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
import tempfile
import socket
import queue
import threading


# =========================
# CRC 错误注入开关
# =========================
INJECT_CRC = False         # 总开关
INJECT_PERIOD = 5       # 每 N 帧注入一次错误
_tx_cnt = 0             # 发送帧计数器（全局）
# 让脚本无论从哪里启动，都能 import Ai_module
PROJECT_ROOT = Path(__file__).resolve().parents[1]  # Final_Project
sys.path.insert(0, str(PROJECT_ROOT))

# ===== Frame codec（阶段三新增）=====
# 要求你的 frame_codec.py 在 PROJECT_ROOT 下，或在 sys.path 能找到
from frame_codec import try_parse_frame, pack_frame
from image_chunk_protocol import ImageChunkAssembler

# ===== AI imports（沿用阶段二）=====
from Ai_module.ai_infer import YoloInfer
from Ai_module.binary_decision import binary_insect_decision_debug as binary_decision

# ===== AI 推理参数 =====
YOLO_CONF = 0.45
DECISION_CONF = 0.45
IMG_SIZE = 320
IOU = 0.30

# ===== 串口参数 =====
PORT = None  # None = 自动检测 CH340/CH341 串口
BAUD = 115200

# ===== 模型与测试数据 =====
MODEL_PATH = r"C:\Users\linzh\Desktop\Final_Project\runs_mvp\insect_det_v7_mvp3\weights\best.pt"


# ===== Frame Types（按你 MCU 端约定）=====
TYPE_REQ = 0x01
TYPE_RESULT = 0x02
TYPE_IMAGE = 0x03   

# ===== 分片重组 =====
ASSEMBLY_TIMEOUT_SEC = 5.0
assembler = ImageChunkAssembler(timeout_sec=ASSEMBLY_TIMEOUT_SEC)

# ===== 是否保存重组图像（调试用）=====
SAVE_REASSEMBLED_IMAGE = True
SAVE_DIR = Path(r"C:\Users\linzh\Desktop\Final_Project\Phase3_change\reassembled_output")
SAVE_DIR.mkdir(parents=True, exist_ok=True)

# ===== 初始化 AI（只做一次，别放循环里）=====
infer_engine = None

# ===== 结果缓存（由收到的最新图像更新）=====
_cache_label = "NO"
_cache_conf = 0.0
_cache_ts = 0.0
_last_img_id = None

# ===== AI 异步执行 =====
INFERENCE_QUEUE_SIZE = 1
inference_queue = queue.Queue(maxsize=INFERENCE_QUEUE_SIZE)
stop_event = threading.Event()

# ==== TCP通信 ====

TCP_HOST = "127.0.0.1"
TCP_PORT = 5000

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

def init_tcp_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind((TCP_HOST, TCP_PORT))
    s.listen(1)

    print(f"[PC] TCP server listening on {TCP_HOST}:{TCP_PORT}")

    conn, addr = s.accept()
    print(f"[PC] TCP client connected: {addr}")

    conn.setblocking(False)   # ✅ 放在这里

    return conn

def init_ai():
    global infer_engine
    infer_engine = YoloInfer(MODEL_PATH)
 

def infer_jpeg_bytes(jpeg_bytes: bytes):
    """
    将完整 JPEG bytes 临时落盘，再复用你现有 infer_image(path) 接口。
    作用：
        执行AI推理，并更新结果缓存
    设计：
        -使用临时文件复用现有接口
        -推理结果写入全局cache变量
    
    cache语义：
    _cache_Label/_conf = “系统当前最新状态”
    REQ只读取cache，不触发推理
    """
    global _cache_label, _cache_conf, _cache_ts

    with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as f:
        temp_path = f.name
        f.write(jpeg_bytes)

    try:
        dets = infer_engine.infer_image(
            temp_path,
            conf=YOLO_CONF,
            iou=IOU,
            imgsz=IMG_SIZE,
            save=False,
            save_txt=False,
        )
        info = binary_decision(dets, conf_threshold=DECISION_CONF, min_count=1)

        _cache_label = "YES" if info.y == 1 else "NO"
        _cache_conf = float(info.max_confidence)
        _cache_ts = time.time()

        print(f"[PC] AI updated -> {_cache_label} {_cache_conf:.2f}")

    finally:
        try:
            Path(temp_path).unlink(missing_ok=True)
        except Exception:
            pass

def enqueue_inference(img_id: int, jpeg_bytes: bytes):
    """
    将完整图像送入 AI 推理队列。
    策略：
        - 队列满时，丢弃旧的待推理图像，保留最新图像
    """
    try:
        if inference_queue.full():
            old_item = inference_queue.get_nowait()
            inference_queue.task_done()
            print(f"[AI] drop old pending img_id={old_item[0]}")

        inference_queue.put_nowait((img_id, jpeg_bytes))
        print(f"[AI] enqueued img_id={img_id}, bytes={len(jpeg_bytes)}")

    except Exception as e:
        print(f"[AI] enqueue ERROR img_id={img_id}: {e}")

def ai_worker():
    """
    AI 后台工作线程：
        - 从 inference_queue 取完整 JPEG
        - 执行推理
        - 更新结果缓存
    """
    while not stop_event.is_set():
        try:
            item = inference_queue.get(timeout=0.2)
        except queue.Empty:
            continue

        if item is None:
            inference_queue.task_done()
            break

        img_id, jpeg_bytes = item

        try:
            print(f"[AI] start img_id={img_id}, bytes={len(jpeg_bytes)}")
            infer_jpeg_bytes(jpeg_bytes)
            print(f"[AI] done img_id={img_id}, result={_cache_label}, conf={_cache_conf:.2f}")
        except Exception as e:
            print(f"[AI] ERROR img_id={img_id}: {e}")
        finally:
            inference_queue.task_done()

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
        global _last_img_id
        _last_img_id = result.img_id
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

        # ===== AI异步推理 =====
        enqueue_inference(result.img_id, jpeg_bytes)
def build_result_frame(req_seq: int) -> bytes:
    """
    根据最新缓存结果构造返回给 MCU 的 RESULT 帧
    """
    payload = b"YES" if _cache_label == "YES" else b"NO!"
    out = pack_frame(payload, frame_type=TYPE_RESULT, seq=req_seq)

    global _tx_cnt
    _tx_cnt += 1
    if INJECT_CRC and (_tx_cnt % INJECT_PERIOD == 0):
        print("[PC] >>> Injecting CRC ERROR")
        out = bytearray(out)
        out[-6] ^= 0xFF   # 改 CRC 一个字节，不碰 EOF
        out = bytes(out)

    return out

def main():
    '''
    主循环职责：
        1.UART：接收REQ -> 返回RESULT
        2.TCP：接收IMAGE_CHUNK -> 重组 -> AI推理 -> 更新缓存
        3.cleanup：清理超时未完成图像

    特点：
        -不依赖线程
        -通过轮询实现多通道处理
    '''
    init_ai()
        # ===== 启动 AI 后台线程 =====
    worker = threading.Thread(target=ai_worker, daemon=True)
    worker.start()
    # TCP通道
    tcp_conn = init_tcp_server()
    tcp_buf = b""

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

    buf = b""

    try:
        while True:
            chunk = ser.read(4096)
            if chunk:
                buf += chunk

            while True:
                frame, buf, err = try_parse_frame(buf)
                if err:
                    print("[PC] PARSE_ERR:", err)
                if not frame:
                    break

                if frame.type == TYPE_IMAGE:
                    handle_image_frame(frame)
                    continue

                if frame.type == TYPE_REQ:
                    out = build_result_frame(frame.seq)
                    ser.write(out)
                    ser.flush()

                    print(
                        f"[PC] RX REQ seq={frame.seq} "
                        f"-> TX {(_cache_label.encode() if _cache_label == 'YES' else b'NO!')!r} "
                        f"(conf={_cache_conf:.2f}, last_img_id={_last_img_id})"
                    )
                    continue

            # ===== TCP 接收 IMAGE =====
            try:
                tcp_chunk = tcp_conn.recv(4096)
                if tcp_chunk:
                    tcp_buf += tcp_chunk
            except BlockingIOError:
                pass
            except Exception:
                print("[PC] TCP disconnected")
                tcp_conn.close()
                tcp_conn = init_tcp_server()
                tcp_buf = b""

            while True:
                frame, tcp_buf, err = try_parse_frame(tcp_buf)
                if err:
                    print("[PC][TCP] PARSE_ERR:", err)
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
        stop_event.set()

        try:
            inference_queue.put_nowait(None)
        except Exception:
            pass

        try:
            ser.close()
        except Exception:
            pass

        print("[PC] Serial closed")
if __name__ == "__main__":
    main()