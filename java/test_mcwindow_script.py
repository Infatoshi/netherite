import json
import socket
import struct
import threading

from mcwindow_script import MAGIC, MCWindowClient, load_script


def test_load_script_validates_and_normalizes(tmp_path):
    script = tmp_path / "input.jsonl"
    script.write_text(
        '{"seconds":0.1,"keys":["w","w"],"buttons":[1],"look":[2,-3]}\n'
    )
    assert load_script(script) == [{
        "seconds": 0.1, "keys": ["w"], "buttons": [1], "look": [2, -3],
    }]


def test_client_drains_frames_and_releases_inputs():
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    received = []

    def serve():
        conn, _ = listener.accept()
        conn.sendall(struct.pack(">iiii", MAGIC, 854, 480, 1) + b"x")
        data = b""
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            data += chunk
        received.extend(
            json.loads(line) for line in data.splitlines() if line.strip()
        )
        conn.close()

    thread = threading.Thread(target=serve)
    thread.start()
    with MCWindowClient("127.0.0.1", listener.getsockname()[1]) as client:
        client.run([{
            "seconds": 0.01, "keys": ["w"], "buttons": [1], "look": [2, -3],
        }])
        assert client.frames == 1
        assert (client.width, client.height) == (854, 480)
    thread.join(timeout=2)
    listener.close()
    assert {"t": "key", "sym": "w", "p": 1} in received
    assert {"t": "key", "sym": "w", "p": 0} in received
    assert {"t": "mb", "b": 1, "p": 1} in received
    assert {"t": "mb", "b": 1, "p": 0} in received
    assert {"t": "look", "dx": 2.0, "dy": -3.0} in received
