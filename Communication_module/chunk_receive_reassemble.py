from __future__ import annotations

import socket
import time
from pathlib import Path

from frame_codec import try_parse_frame
from image_chunk_protocol import ImageChunkAssembler


# ===== TCP 参数 =====
TCP_HOST = "127.0.0.1"
TCP_PORT = 5000

# ===== 输出目录 =====
SAVE_DIR = Path(r"C:\Users\linzh\Desktop\Final_Project\reassembled_output")
SAVE_DIR.mkdir(parents=True, exist_ok=True)

# ===== 接收参数 =====
RECV_BUF_SIZE = 4096
ASSEMBLY_TIMEOUT_SEC = 5.0


def main() -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((TCP_HOST, TCP_PORT))
    server.listen(1)

    print(f"[RX] Listening on {TCP_HOST}:{TCP_PORT}")
    print(f"[RX] Save dir: {SAVE_DIR}")

    conn = None
    try:
        conn, addr = server.accept()
        conn.settimeout(5.0)
        print(f"[RX] Client connected from {addr}")

        recv_buffer = b""
        assembler = ImageChunkAssembler(timeout_sec=ASSEMBLY_TIMEOUT_SEC)

        while True:
            try:
                data = conn.recv(RECV_BUF_SIZE)
            except socket.timeout:
                expired = assembler.cleanup()
                for img_id in expired:
                    print(f"[RX] timeout drop img_id={img_id}")
                continue

            if not data:
                print("[RX] Client disconnected")
                break

            recv_buffer += data
            print(f"[RX] recv bytes={len(data)}, buffer={len(recv_buffer)}")

            while True:
                frame, recv_buffer, err = try_parse_frame(recv_buffer)

                if err:
                    print(f"[RX] parse err: {err}")

                if frame is None:
                    break

                print(
                    f"[RX] frame ok: "
                    f"type=0x{frame.type:02X}, "
                    f"seq={frame.seq}, "
                    f"payload_len={len(frame.payload)}"
                )

                result = assembler.handle_frame(frame)
                if result is not None:
                    ts = time.strftime("%Y%m%d_%H%M%S")
                    out_path = SAVE_DIR / f"img_{result.img_id}_{ts}.jpg"

                    with open(out_path, "wb") as f:
                        f.write(result.data)

                    print(
                        f"[RX] IMAGE COMPLETE: "
                        f"img_id={result.img_id}, "
                        f"chunks={result.total_chunks}, "
                        f"bytes={len(result.data)}"
                    )
                    print(f"[RX] saved: {out_path}")

    finally:
        if conn is not None:
            conn.close()
        server.close()
        print("[RX] Server closed")


if __name__ == "__main__":
    main()