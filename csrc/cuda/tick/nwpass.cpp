// The engine's C on the device (GPU plan L17): an LLVM pass plugin for the
// two clang builds of csrc/engine that cuda/tick/tick.mk makes, the host's (x86-64
// objects for out/native/cuda/tick/test_snapshots) and the device's (nvptx64
// bitcode, linked with cuda/tick/dev.c into one module, then split into parts
// that are compiled one at a time and device-linked).
//
// nw-rename (both builds, at the start of every module's pipeline, through
// clang -fpass-plugin): every internal or private global and function is
// renamed FILE$NAME (FILE the source's base name) and made internal, so the
// host executable's symbol table names every one of them and the two builds
// name each the same way. The device side finds a host symbol by its name.
//
// nw-roots (the whole device module): keeps the kernels, the runtime and the
// callbacks named by -nw-callbacks with what they reach by direct calls.
//
// nw-stub (the whole device module, after it is optimized): a function the
// device build does not have (the C library's, zlib's) or cannot follow
// (inline asm, a pointer inside an aggregate load or store) gets a body that
// flags the run (nwdev_unsupported) and returns zero; the run is redone in C.
// Then nwdev_syms: every global variable and every function whose address is
// taken, with its device address, size, name and flags, which the host side
// resolves against its own symbol table (cuda/tick/host.c).
//
// nw-dev (each part, with cuda/tick/inl.c linked in). The device runs the image
// at the host's addresses (the child maps device memory at the image's own
// address), so an image pointer is the same on both; a global's or a
// function's is not. Memory always holds the host's form, registers the
// device's:
//   - every load of a pointer passes the value through nwdev_in (a host
//     global or function address becomes the device's);
//   - every store of a pointer passes it through nwdev_out (the reverse);
//   - every write (store, memset, memcpy, memmove, atomics) passes its
//     address and length through nwdev_wb first, which marks the image
//     pages the block's environment wrote and flags a write where the device
//     holds only zeros (the run is then redone in C).
// Functions named nwdev_* (the runtime itself) are left as they are.
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/TargetParser/Triple.h"

#include <cctype>
#include <set>

#include <string>
#include <vector>

using namespace llvm;

namespace {

// the device runtime (cuda/tick/dev.c, inl.c): its nwdev_ functions and its
// kernels (chunkloop, tick_scatter and the others) are neither stubbed nor
// instrumented; the engine's C has no kernel of its own
bool runtime(const Function &F)
{
    return F.getName().starts_with("nwdev_") || F.getCallingConv() == CallingConv::PTX_Kernel;
}

std::string base_name(const std::string &path)
{
    size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

struct NwRename : PassInfoMixin<NwRename> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &)
    {
        std::string prefix = base_name(M.getSourceFileName()) + "$";
        unsigned anon = 0;
        bool changed = false;
        // NVPTX uniques a repeated name without the dot (.str, .str1, .str2
        // where the host has .str, .str.1, .str.2): put the dot back
        bool nvptx = Triple(M.getTargetTriple()).isNVPTX();
        std::set<std::string> names;
        for (GlobalVariable &G : M.globals()) names.insert(G.getName().str());
        for (Function &F : M) names.insert(F.getName().str());
        auto canon = [&](const std::string &n) -> std::string {
            if (!nvptx) return n;
            size_t k = n.size();
            while (k > 0 && isdigit((unsigned char)n[k - 1])) --k;
            if (k == 0 || k == n.size() || !names.count(n.substr(0, k))) return n;
            return n.substr(0, k) + "." + n.substr(k);
        };
        for (GlobalVariable &G : M.globals())
        {
            if (!G.hasLocalLinkage() || G.getName().starts_with("llvm.")) continue;
            std::string n = G.hasName() ? canon(G.getName().str()) : "anon." + std::to_string(anon++);
            if (n.rfind(prefix, 0) == 0) continue;
            G.setName(prefix + n);
            if (G.hasPrivateLinkage()) G.setLinkage(GlobalValue::InternalLinkage);
            changed = true;
        }
        for (Function &F : M)
        {
            if (F.isDeclaration() || !F.hasLocalLinkage()) continue;
            std::string n = F.hasName() ? canon(F.getName().str()) : "anonfn." + std::to_string(anon++);
            if (n.rfind(prefix, 0) == 0) continue;
            F.setName(prefix + n);
            if (F.hasPrivateLinkage()) F.setLinkage(GlobalValue::InternalLinkage);
            changed = true;
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
    static bool isRequired() { return true; }
};

bool type_has_ptr(Type *t)
{
    if (t->isPointerTy()) return true;
    if (auto *v = dyn_cast<VectorType>(t)) return v->getElementType()->isPointerTy();
    if (auto *a = dyn_cast<ArrayType>(t)) return type_has_ptr(a->getElementType());
    if (auto *s = dyn_cast<StructType>(t))
    {
        for (Type *e : s->elements())
            if (type_has_ptr(e)) return true;
    }
    return false;
}

// The module's roots: the kernels, the runtime, and the host callbacks the
// device may call (-nw-callbacks=NAME,...: what the tick stores in the image
// and S6 calls through, st_on_block and the rest), with everything they
// reach by direct calls. Everything else goes (the module is the whole engine
// before this). An indirect call to a function outside the module (a chunk
// provider, a pressure plate's entity query) loads a host address nwdev_in
// has no device counterpart for: that run is flagged and redone in C.
cl::opt<std::string> NwCallbacks("nw-callbacks", cl::desc("the host callbacks the device module keeps"), cl::init(""));

struct NwRoots : PassInfoMixin<NwRoots> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &)
    {
        std::set<Function *> reach;
        std::vector<Function *> work;
        auto add = [&](Function *F) {
            if (F != nullptr && !F->isDeclaration() && reach.insert(F).second) work.push_back(F);
        };
        for (Function &F : M)
            if (F.getCallingConv() == CallingConv::PTX_Kernel || F.getName().starts_with("nwdev_")) add(&F);
        std::string list = NwCallbacks;
        size_t missing = 0, pos = 0;
        while (pos <= list.size() && !list.empty())
        {
            size_t e = list.find(',', pos);
            std::string n = list.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            if (!n.empty())
            {
                Function *F = M.getFunction(n);
                if (F == nullptr || F->isDeclaration())
                {
                    errs() << "nw-roots: callback " << n << " is not in the module\n";
                    ++missing;
                }
                add(F);
            }
            if (e == std::string::npos) break;
            pos = e + 1;
        }
        size_t indirect = 0;
        while (!work.empty())
        {
            Function *F = work.back();
            work.pop_back();
            for (Instruction &I : instructions(*F))
            {
                if (auto *CB = dyn_cast<CallBase>(&I))
                {
                    if (Function *callee = CB->getCalledFunction()) add(callee);
                    else if (!CB->isInlineAsm()) ++indirect;
                }
                for (Value *op : I.operands())
                    if (auto *G = dyn_cast<Function>(op->stripPointerCasts())) add(G);
            }
        }
        std::vector<GlobalValue *> keep(reach.begin(), reach.end());
        appendToCompilerUsed(M, keep);
        errs() << "nw-roots: " << reach.size() << " functions kept, " << indirect << " indirect calls in them\n";
        if (missing) report_fatal_error("nw-roots: a callback is missing");
        return PreservedAnalyses::none();
    }
    static bool isRequired() { return true; }
};

// what the pass cannot follow in a function: the reason, or nullptr
const char *unfollowable(Function &F)
{
    for (Instruction &I : instructions(F))
    {
        if (auto *CB = dyn_cast<CallBase>(&I))
            if (CB->isInlineAsm()) return "inline asm";
        if (auto *L = dyn_cast<LoadInst>(&I))
        {
            if (L->getPointerAddressSpace() != 0) continue;
            Type *t = L->getType();
            if (t->isPointerTy() && t->getPointerAddressSpace() != 0) return "a load of a non-generic pointer";
            if (!t->isPointerTy() && type_has_ptr(t)) return "a load of an aggregate or vector holding pointers";
        }
        if (auto *S = dyn_cast<StoreInst>(&I))
        {
            if (S->getPointerAddressSpace() != 0) continue;
            Type *t = S->getValueOperand()->getType();
            if (!t->isPointerTy() && type_has_ptr(t)) return "a store of an aggregate or vector holding pointers";
        }
        if (auto *A = dyn_cast<AtomicRMWInst>(&I))
            if (A->getValOperand()->getType()->isPointerTy()) return "an atomic on a pointer";
        if (auto *X = dyn_cast<AtomicCmpXchgInst>(&I))
            if (X->getNewValOperand()->getType()->isPointerTy()) return "a compare-exchange of a pointer";
        if (auto *CB = dyn_cast<CallBase>(&I))
        {
            Function *callee = CB->getCalledFunction();
            if (callee == nullptr || !callee->isIntrinsic() || callee->onlyReadsMemory() || isa<MemIntrinsic>(&I)) continue;
            StringRef n = callee->getName();
            static const char *const ok[] = {"llvm.lifetime", "llvm.assume", "llvm.trap", "llvm.nvvm", "llvm.experimental.noalias",
                                             "llvm.stacksave", "llvm.stackrestore", "llvm.va_start", "llvm.va_end", "llvm.va_copy",
                                             "llvm.memset", "llvm.memcpy", "llvm.memmove", "llvm.dbg", "llvm.ubsantrap",
                                             "llvm.sideeffect", "llvm.donothing", "llvm.pseudoprobe"};
            bool fine = false;
            for (const char *o : ok) fine |= n.starts_with(o);
            if (!fine) return "an intrinsic that writes memory";
        }
    }
    return nullptr;
}

std::string sanitize(StringRef n)
{
    std::string o;
    for (char c : n)
    {
        if (c == '.') o += "_$_";
        else o += c;
    }
    return o;
}

GlobalVariable *private_string(Module &M, StringRef str, const char *name)
{
    Constant *s = ConstantDataArray::getString(M.getContext(), str);
    auto *g = new GlobalVariable(M, s->getType(), true, GlobalValue::PrivateLinkage, s, name);
    g->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    return g;
}

struct NwStub : PassInfoMixin<NwStub> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &)
    {
        LLVMContext &C = M.getContext();
        const DataLayout &DL = M.getDataLayout();
        Type *P = PointerType::get(C, 0);
        Type *I64 = Type::getInt64Ty(C);
        Type *I32 = Type::getInt32Ty(C);
        FunctionCallee funsup = M.getOrInsertFunction("nwdev_unsupported", Type::getVoidTy(C), P);
        // a body that flags the run with the function's name and returns zero
        auto stub = [&](Function &F, const char *why) {
            std::string label = (F.getName() + " (" + why + ")").str();
            if (!F.isDeclaration()) F.deleteBody();
            F.setLinkage(GlobalValue::InternalLinkage);
            BasicBlock *bb = BasicBlock::Create(C, "", &F);
            IRBuilder<> B(bb);
            B.CreateCall(funsup, {private_string(M, label, "nwdev_unsup")});
            if (F.getReturnType()->isVoidTy()) B.CreateRetVoid();
            else B.CreateRet(Constant::getNullValue(F.getReturnType()));
        };
        long stubbed = 0, undefined = 0, globals_defined = 0;
        for (Function &F : M)
        {
            if (runtime(F) || F.isIntrinsic()) continue;
            if (F.isDeclaration())
            {
                if (F.getName() == "vprintf" || F.use_empty()) continue;
                errs() << "nw-stub: " << F.getName() << " is not in the device build, stubbed\n";
                stub(F, "not in the device build");
                ++undefined;
                continue;
            }
            if (const char *why = unfollowable(F))
            {
                errs() << "nw-stub: " << F.getName() << ": " << why << ", stubbed\n";
                stub(F, why);
                ++stubbed;
            }
        }
        for (GlobalVariable &G : M.globals())
            if (G.isDeclaration() && !G.getName().starts_with("llvm.") && !G.getName().starts_with("nwdev_") && !G.use_empty())
            {
                G.setInitializer(Constant::getNullValue(G.getValueType()));
                G.setLinkage(GlobalValue::InternalLinkage);
                ++globals_defined;
            }

        // the symbol table
        StructType *ent = StructType::get(C, {P, I64, P, I32, I32});
        std::vector<Constant *> rows;
        std::vector<GlobalVariable *> gs;
        for (GlobalVariable &G : M.globals())
        {
            if (G.isDeclaration() || G.getName().starts_with("llvm.") || G.getName().starts_with("nwdev_") ||
                G.getAddressSpace() != 0 || !G.hasName())
                continue;
            gs.push_back(&G);
        }
        for (GlobalVariable *G : gs)
        {
            uint32_t flags = (G->isConstant() ? 1u : 0u) | (type_has_ptr(G->getValueType()) ? 2u : 0u);
            rows.push_back(ConstantStruct::get(ent, {G, ConstantInt::get(I64, DL.getTypeAllocSize(G->getValueType())),
                                                     private_string(M, G->getName(), "nwdev_name"),
                                                     ConstantInt::get(I32, flags), ConstantInt::get(I32, 0)}));
        }
        for (Function &F : M)
        {
            if (F.isDeclaration() || F.getName().starts_with("nwdev_") || !F.hasAddressTaken()) continue;
            if (F.getCallingConv() == CallingConv::PTX_Kernel) continue;
            rows.push_back(ConstantStruct::get(ent, {&F, ConstantInt::get(I64, 0), private_string(M, F.getName(), "nwdev_name"),
                                                     ConstantInt::get(I32, 4), ConstantInt::get(I32, 0)}));
        }
        ArrayType *at = ArrayType::get(ent, rows.size());
        new GlobalVariable(M, at, true, GlobalValue::ExternalLinkage, ConstantArray::get(at, rows), "nwdev_syms");
        new GlobalVariable(M, I32, true, GlobalValue::ExternalLinkage, ConstantInt::get(I32, rows.size()), "nwdev_nsyms");
        // PTX names take no dots; NVPTX fixes internal ones itself, but the
        // parts' cross references are external after the split (the table
        // keeps the host's names as strings)
        for (GlobalVariable &G : M.globals())
            if (G.getName().contains('.') && !G.getName().starts_with("llvm.")) G.setName(sanitize(G.getName()));
        for (Function &F : M)
            if (F.getName().contains('.') && !F.isIntrinsic()) F.setName(sanitize(F.getName()));
        errs() << "nw-stub: " << stubbed << " functions stubbed, " << undefined << " undefined ones stubbed, "
               << globals_defined << " undefined globals zeroed; " << rows.size() << " symbols\n";
        return PreservedAnalyses::none();
    }
    static bool isRequired() { return true; }
};

// the shortest constant length nw-dev hands to the runtime's wide copies:
// below it the backend expands the intrinsic into moves itself
constexpr uint64_t WIDE_MIN = 128;

struct NwDev : PassInfoMixin<NwDev> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &)
    {
        const DataLayout &DL = M.getDataLayout();
        Function *fin = M.getFunction("nwdev_in"), *fout = M.getFunction("nwdev_out"), *fwb = M.getFunction("nwdev_wb");
        Function *fcallee = M.getFunction("nwdev_callee");
        if (fin == nullptr || fout == nullptr || fwb == nullptr || fcallee == nullptr)
            report_fatal_error("nw-dev: link cuda/tick/inl.c in first");
        Type *I64 = Type::getInt64Ty(M.getContext());
        Type *Ptr = PointerType::get(M.getContext(), 0);
        FunctionCallee fcpy = M.getOrInsertFunction("nwdev_memcpy", FunctionType::get(Ptr, {Ptr, Ptr, I64}, false));
        FunctionCallee fmove = M.getOrInsertFunction("nwdev_memmove", FunctionType::get(Ptr, {Ptr, Ptr, I64}, false));
        FunctionCallee fset = M.getOrInsertFunction("nwdev_memset", FunctionType::get(Ptr, {Ptr, Type::getInt32Ty(M.getContext()), I64}, false));
        long loads = 0, stores = 0, writes = 0, calls = 0, wide = 0;
        for (Function &F : M)
        {
            if (F.isDeclaration() || runtime(F)) continue;
            std::vector<Instruction *> work;
            for (Instruction &I : instructions(F)) work.push_back(&I);
            for (Instruction *I : work)
            {
                IRBuilder<> B(I);
                if (auto *L = dyn_cast<LoadInst>(I))
                {
                    if (L->getPointerAddressSpace() != 0 || !L->getType()->isPointerTy()) continue;
                    B.SetInsertPoint(L->getNextNode());
                    CallInst *c = B.CreateCall(fin, {L});
                    L->replaceUsesWithIf(c, [c](Use &U) { return U.getUser() != c; });
                    ++loads;
                    continue;
                }
                if (auto *S = dyn_cast<StoreInst>(I))
                {
                    if (S->getPointerAddressSpace() != 0) continue;
                    Value *v = S->getValueOperand();
                    if (v->getType()->isPointerTy())
                    {
                        S->setOperand(0, B.CreateCall(fout, {v, S->getPointerOperand()}));
                        ++stores;
                    }
                    uint64_t n = DL.getTypeStoreSize(v->getType());
                    S->setOperand(1, B.CreateCall(fwb, {S->getPointerOperand(), ConstantInt::get(I64, n)}));
                    ++writes;
                    continue;
                }
                if (auto *A = dyn_cast<AtomicRMWInst>(I))
                {
                    if (A->getPointerAddressSpace() != 0) continue;
                    uint64_t n = DL.getTypeStoreSize(A->getValOperand()->getType());
                    A->setOperand(0, B.CreateCall(fwb, {A->getPointerOperand(), ConstantInt::get(I64, n)}));
                    ++writes;
                    continue;
                }
                if (auto *X = dyn_cast<AtomicCmpXchgInst>(I))
                {
                    if (X->getPointerAddressSpace() != 0) continue;
                    uint64_t n = DL.getTypeStoreSize(X->getNewValOperand()->getType());
                    X->setOperand(0, B.CreateCall(fwb, {X->getPointerOperand(), ConstantInt::get(I64, n)}));
                    ++writes;
                    continue;
                }
                if (auto *MI = dyn_cast<MemIntrinsic>(I))
                {
                    if (MI->getDestAddressSpace() != 0) continue;
                    Value *len = B.CreateZExtOrTrunc(MI->getLength(), I64);
                    Value *dst = B.CreateCall(fwb, {MI->getRawDest(), len});
                    MI->setDest(dst);
                    ++writes;
                    /* the NVPTX backend makes a mem intrinsic of an unknown
                     * or a large length a loop of byte loads and stores (a
                     * chunk section's 4,128-byte copy: 4,128 of each); the
                     * runtime's copies go by words where both ends allow */
                    auto *CL = dyn_cast<ConstantInt>(MI->getLength());
                    if (MI->isVolatile() || (CL != nullptr && CL->getZExtValue() < WIDE_MIN)) continue;
                    if (auto *MT = dyn_cast<MemTransferInst>(MI))
                    {
                        if (MT->getSourceAddressSpace() != 0) continue;
                        B.CreateCall(isa<MemMoveInst>(MT) ? fmove : fcpy, {dst, MT->getRawSource(), len});
                    }
                    else if (auto *MS = dyn_cast<MemSetInst>(MI))
                        B.CreateCall(fset, {dst, B.CreateZExt(MS->getValue(), Type::getInt32Ty(M.getContext())), len});
                    else continue;
                    MI->eraseFromParent();
                    ++wide;
                    continue;
                }
                if (auto *CB = dyn_cast<CallBase>(I))
                    if (CB->getCalledFunction() == nullptr && !CB->isInlineAsm())
                    {
                        B.CreateCall(fcallee, {CB->getCalledOperand()});
                        ++calls;
                    }
            }
        }
        errs() << "nw-dev: " << loads << " pointer loads, " << stores << " pointer stores, " << writes
               << " writes instrumented, " << calls << " indirect calls checked, " << wide << " mem calls made wide\n";
        return PreservedAnalyses::none();
    }
    static bool isRequired() { return true; }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "netherite", "1", [](PassBuilder &PB) {
                PB.registerPipelineStartEPCallback(
                    [](ModulePassManager &MPM, OptimizationLevel) { MPM.addPass(NwRename()); });
                PB.registerPipelineParsingCallback(
                    [](StringRef name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                        if (name == "nw-rename") { MPM.addPass(NwRename()); return true; }
                        if (name == "nw-dev") { MPM.addPass(NwDev()); return true; }
                        if (name == "nw-roots") { MPM.addPass(NwRoots()); return true; }
                        if (name == "nw-stub") { MPM.addPass(NwStub()); return true; }
                        return false;
                    });
            }};
}
