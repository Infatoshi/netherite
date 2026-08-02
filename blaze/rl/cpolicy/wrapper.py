"""ctypes wrapper for cpolicy_fwd.so (fused C/CUDA rollout policy)."""
from __future__ import annotations

import ctypes
import os

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
SO_PATH = os.path.join(HERE, "cpolicy_fwd.so")

# Must match IRON_CHAIN=0 ChainPolicy
HEADS = (3, 3, 3, 2, 2, 2, 7, 2, 10)
NHEAD = len(HEADS)
NLOGITS = sum(HEADS)  # 34
NCH, CAM_H, CAM_W = 18, 36, 64
NSCAL = 27
FC_IN = 6272 + NSCAL  # 6299
FC_OUT = 256

_LIB = None


def _lib():
    global _LIB
    if _LIB is not None:
        return _LIB
    if not os.path.exists(SO_PATH):
        raise FileNotFoundError(
            f"{SO_PATH} missing; build with: "
            f"make -C magma cpolicy_so BLAZE_SM=\"sm_86 sm_120\"")
    lib = ctypes.CDLL(SO_PATH)
    lib.cpolicy_create.restype = ctypes.c_void_p
    lib.cpolicy_create.argtypes = [ctypes.c_int, ctypes.c_int]
    lib.cpolicy_destroy.argtypes = [ctypes.c_void_p]
    lib.cpolicy_upload_weights.restype = ctypes.c_int
    lib.cpolicy_upload_weights.argtypes = (
        [ctypes.c_void_p] + [ctypes.c_void_p] * 10 + [ctypes.c_int])
    lib.cpolicy_forward_sample.restype = ctypes.c_int
    lib.cpolicy_forward_sample.argtypes = [
        ctypes.c_void_p,  # h
        ctypes.c_void_p,  # obs
        ctypes.c_void_p,  # scal
        ctypes.c_void_p,  # burnin
        ctypes.c_void_p,  # noop
        ctypes.c_int,     # n
        ctypes.c_int,     # mode
        ctypes.c_uint64,  # seed
        ctypes.c_void_p,  # stream
        ctypes.c_void_p,  # acts
        ctypes.c_void_p,  # logp
        ctypes.c_void_p,  # value
        ctypes.c_void_p,  # entropy
        ctypes.c_void_p,  # logits
    ]
    lib.cpolicy_last_error.restype = ctypes.c_char_p
    lib.cpolicy_last_error.argtypes = []
    _LIB = lib
    return lib


def pack_heads(net):
    """Pack ModuleList head weights into [34,256] and [34] on the same device."""
    ws, bs = [], []
    for head in net.heads:
        ws.append(head.weight.detach())
        bs.append(head.bias.detach())
    return torch.cat(ws, dim=0).contiguous(), torch.cat(bs, dim=0).contiguous()


class CPolicyFwd:
    """Own a cpolicy handle; upload weights from a ChainPolicy; sample."""

    def __init__(self, device: int, max_n: int):
        self.device = int(device)
        self.max_n = int(max_n)
        lib = _lib()
        self.h = lib.cpolicy_create(self.device, self.max_n)
        if not self.h:
            raise RuntimeError(
                f"cpolicy_create failed: {lib.cpolicy_last_error().decode()}")
        self._lib = lib
        # reusable output buffers, grown to max_n
        dev = torch.device(f"cuda:{self.device}")
        self._acts = torch.empty((max_n, NHEAD), dtype=torch.int64, device=dev)
        self._logp = torch.empty(max_n, dtype=torch.float32, device=dev)
        self._value = torch.empty(max_n, dtype=torch.float32, device=dev)
        self._entropy = torch.empty(max_n, dtype=torch.float32, device=dev)
        self._logits = torch.empty((max_n, NLOGITS), dtype=torch.float32,
                                   device=dev)
        self._burnin_u8 = torch.empty(max_n, dtype=torch.uint8, device=dev)
        self._rng_seed = 0
        self._uploaded = False

    def close(self):
        if self.h:
            self._lib.cpolicy_destroy(ctypes.c_void_p(self.h))
            self.h = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def upload_from_net(self, net):
        """Copy current torch weights into the C handle (device->device)."""
        sd = net.state_dict()
        # state_dict keys for ChainPolicy:
        # conv.0.weight/bias, conv.2.weight/bias, fc.0.weight/bias,
        # heads.i.weight/bias, value.weight/bias
        c1w = sd["conv.0.weight"].detach().contiguous()
        c1b = sd["conv.0.bias"].detach().contiguous()
        c2w = sd["conv.2.weight"].detach().contiguous()
        c2b = sd["conv.2.bias"].detach().contiguous()
        fcw = sd["fc.0.weight"].detach().contiguous()
        fcb = sd["fc.0.bias"].detach().contiguous()
        hw, hb = pack_heads(net)
        vw = sd["value.weight"].detach().contiguous().view(-1)
        vb = sd["value.bias"].detach().contiguous()
        rc = self._lib.cpolicy_upload_weights(
            ctypes.c_void_p(self.h),
            ctypes.c_void_p(c1w.data_ptr()),
            ctypes.c_void_p(c1b.data_ptr()),
            ctypes.c_void_p(c2w.data_ptr()),
            ctypes.c_void_p(c2b.data_ptr()),
            ctypes.c_void_p(fcw.data_ptr()),
            ctypes.c_void_p(fcb.data_ptr()),
            ctypes.c_void_p(hw.data_ptr()),
            ctypes.c_void_p(hb.data_ptr()),
            ctypes.c_void_p(vw.data_ptr()),
            ctypes.c_void_p(vb.data_ptr()),
            1,  # on_device
        )
        if rc != 0:
            raise RuntimeError(
                f"upload_weights failed: "
                f"{self._lib.cpolicy_last_error().decode()}")
        self._uploaded = True
        # keep refs so storage is not freed before the async copy completes
        self._keep = (c1w, c1b, c2w, c2b, fcw, fcb, hw, hb, vw, vb)

    def forward_sample(self, obs_u8, scal, burnin, noop, mode=0,
                       want_logits=False):
        """Run fused forward+sample.

        mode: 0=Gumbel sample, 1=greedy argmax.
        Returns (acts [n,9] int64, logp [n], value [n], entropy [n]
                 [, logits [n,34]]).
        """
        if not self._uploaded:
            raise RuntimeError("upload_from_net before forward_sample")
        n = int(obs_u8.shape[0])
        if n > self.max_n:
            raise RuntimeError(f"n={n} > max_n={self.max_n}")
        if obs_u8.dtype != torch.uint8 or not obs_u8.is_cuda:
            raise TypeError("obs_u8 must be cuda uint8")
        if scal.dtype != torch.float32 or not scal.is_cuda:
            raise TypeError("scal must be cuda float32")
        # burnin as uint8
        bu = self._burnin_u8[:n]
        bu.copy_(burnin.to(torch.uint8))
        stream = torch.cuda.current_stream(obs_u8.device).cuda_stream
        self._rng_seed = (self._rng_seed + 1) & 0xFFFFFFFFFFFFFFFF
        rc = self._lib.cpolicy_forward_sample(
            ctypes.c_void_p(self.h),
            ctypes.c_void_p(obs_u8.data_ptr()),
            ctypes.c_void_p(scal.data_ptr()),
            ctypes.c_void_p(bu.data_ptr()),
            ctypes.c_void_p(noop.data_ptr()),
            n,
            int(mode),
            ctypes.c_uint64(self._rng_seed),
            ctypes.c_void_p(stream),
            ctypes.c_void_p(self._acts.data_ptr()),
            ctypes.c_void_p(self._logp.data_ptr()),
            ctypes.c_void_p(self._value.data_ptr()),
            ctypes.c_void_p(self._entropy.data_ptr()),
            ctypes.c_void_p(self._logits.data_ptr()) if want_logits else None,
        )
        if rc != 0:
            raise RuntimeError(
                f"forward_sample failed: "
                f"{self._lib.cpolicy_last_error().decode()}")
        out = (self._acts[:n], self._logp[:n], self._value[:n],
               self._entropy[:n])
        if want_logits:
            return out + (self._logits[:n],)
        return out
