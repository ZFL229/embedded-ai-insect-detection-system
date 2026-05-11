"""
PC Frame Service (Chunk + AI Integrated Version)

模块定位：
------------------------------------------------------------
本模块是系统的“通信 + AI 推理服务节点”，负责：

    1. 接收图像分片帧（TCP）
    2. 在通信层完成图像重组（ImageChunkAssembler）
    3. 将完整图像交给 AI 推理
    4. 缓存最新推理结果
    5. 通过 UART 响应 MCU 请求（REQ → RESULT）

系统结构：
------------------------------------------------------------
    MCU / Sender
        ↓
    TCP（图像上行）
        ↓
    PC通信模块（本文件）
        ↓
    AI推理
        ↓
    UART（结果下行）
        ↓
    MCU（LED响应）

设计特点：
------------------------------------------------------------
    - 分片重组在通信层完成（非AI层）
    - AI只处理“完整图像”
    - 结果采用缓存机制（REQ读取）
"""
import serial
from serial.tools import list_ports
import time
import sys
from pathlib import Path

# 让脚本无论从哪里启动，都能 import Ai_module
PROJECT_ROOT = Path(__file__).resolve().parents[1]  # Final_Project
sys.path.insert(0, str(PROJECT_ROOT))

# ===== Frame codec（阶段三新增）=====
# 要求你的 frame_codec.py 在 PROJECT_ROOT 下，或在 sys.path 能找到
from frame_codec import try_parse_frame
from image_chunk_protocol import ImageChunkAssembler

# ===== 串口参数 =====
PORT = None  # None = 自动检测 CH340/CH341 串口
BAUD = 115200

# ===== Frame Types（按你 MCU 端约定）=====
TYPE_IMAGE = 0x03   

# ===== 分片重组 =====
ASSEMBLY_TIMEOUT_SEC = 5.0
assembler = ImageChunkAssembler(timeout_sec=ASSEMBLY_TIMEOUT_SEC)

# ===== 是否保存重组图像（调试用）=====
SAVE_REASSEMBLED_IMAGE = True
SAVE_DIR = Path(r"C:\Users\linzh\Desktop\Final_Project\Communication_module\reassembled_output")
SAVE_DIR.mkdir(parents=True, exist_ok=True)

def auto_detect_serial_port():
    """
    自动检测 CH340/CH341 串口
    返回：
        端口名，例如 "COM8"
    失败：
        返回 None
    匹配策略：
        1. description 中包含 ch340 / ch341
        2. hwid 中包含 1A86（WCH 常见 VID）
        3. manufacturer 中包含 wch
        4. 最后宽松匹配 usb-serial
    """
    ports = list(list_ports.comports())

    if not ports:
        return None

    # 第一轮：优先精确匹配 CH340/CH341 / WCH
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        manuf = (p.manufacturer or "").lower()

        if "ch340" in desc or "ch341" in desc:
            return p.device

        if "1a86" in hwid:
            return p.device

        if "wch" in manuf:
            return p.device

    # 第二轮：宽松匹配 USB 串口设备
    for p in ports:
        text = f"{p.device} {p.description} {p.hwid} {p.manufacturer}".lower()
        if "usb-serial" in text or "usb serial" in text:
            return p.device

    return None

def handle_image_frame(frame):
    """
    分片模式：
    frame.payload = 一个 chunk
    作用：
        接收“图像相关帧”，将驱动分片重组流程
    行为：
        -未完成 -> 返回 None -> 继续等待
        -完成 -> 返回完整JPEG -> 触发AI推理
    注意：
        此函数是：
            通信 -> AI的触发点
    """

    result = assembler.handle_frame(frame)

    if result is not None:
        # ===== 重组完成 =====
        jpeg_bytes = result.data
        print(
            f"[PC] IMAGE COMPLETE: "
            f"img_id={result.img_id}, "
            f"chunks={result.total_chunks}, "
            f"bytes={len(jpeg_bytes)}"
        )

        # ===== 保存（调试用）=====
        if SAVE_REASSEMBLED_IMAGE:
            ts = time.strftime("%Y%m%d_%H%M%S")
            out_path = SAVE_DIR / f"img_{result.img_id}_{ts}.jpg"

            with open(out_path, "wb") as f:
                f.write(jpeg_bytes)

            print(f"[PC] saved: {out_path}")

def wait_for_mcu_standby_and_send_ready(ser):
    """
    PC端握手：
        1. 等待 MCU 周期性发送 STANDBY
        2. 收到 STANDBY 后回复 READY
        3. 清空输入缓冲区，避免握手文本残留影响后续帧解析
    """

    print("[PC] Waiting for MCU STANDBY...")

    buf = b""

    while True:
        data = ser.read(128)

        if data:
            buf += data
            print(f"[PC] HANDSHAKE RX: {data!r}")

            if b"STANDBY" in buf:
                ser.write(b"READY")
                ser.flush()

                print("[PC] STANDBY received")
                print("[PC] READY sent")
                break

        if len(buf) > 1024:
            buf = buf[-128:]

        time.sleep(0.05)

    try:
        ser.reset_input_buffer()
    except Exception:
        pass

def main():
    """
    UART Image Receiver

    职责：
        1. 打开 UART 串口
        2. 等待 MCU 发送 STANDBY
        3. 回复 READY 完成握手
        4. 从 UART 接收字节流
        5. 使用 try_parse_frame() 解析底层帧
        6. 使用 ImageChunkAssembler 重组 JPEG
        7. 将完整图片保存到本地
    """

    # ===== 串口初始化 =====
    port_to_use = PORT if PORT else auto_detect_serial_port()

    if not port_to_use:
        raise RuntimeError("[PC] 未检测到可用串口（CH340/CH341）")

    print(f"[PC] Auto selected serial port: {port_to_use}")

    ser = serial.Serial(port_to_use, BAUD, timeout=0.05)

    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
    except Exception:
        pass

    print(f"[PC] Opened {port_to_use} @ {BAUD}")
    print(f"[PC] Save dir: {SAVE_DIR}")

    # ===== 新增：握手机制 =====
    wait_for_mcu_standby_and_send_ready(ser)

    # ===== 握手完成后，进入正式帧接收 =====
    buf = b""

    try:
        while True:
            chunk = ser.read(4096)

            if chunk:
                buf += chunk
                print(f"[PC] RX bytes={len(chunk)}, buffer={len(buf)}")

            while True:
                frame, buf, err = try_parse_frame(buf)

                if err:
                    print("[PC] PARSE_ERR:", err)

                if not frame:
                    break

                if frame.type == TYPE_IMAGE:
                    handle_image_frame(frame)
                    continue

                print(f"[PC] IGNORE frame.type=0x{frame.type:02X}, seq={frame.seq}")

            expired = assembler.cleanup()
            for img_id in expired:
                print(f"[PC] timeout drop img_id={img_id}")

            time.sleep(0.002)

    finally:
        try:
            ser.close()
        except Exception:
            pass

        print("[PC] Serial closed")
if __name__ == "__main__":
    main()