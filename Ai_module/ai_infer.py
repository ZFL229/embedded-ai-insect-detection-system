# ai_infer.py
# Minimal, engineering-friendly YOLOv8 inference wrapper
# 目标： 把 YOLOv8 的推理过程封装成一个“工程可调用的模块”，方便后续集成到更大的系统中。
# 输入：image_path(图片路径)
# 输出：List[Detection]（检测结果列表）

#为什么要封装？
#1.让“AI推理”与“后续判定/通信/现实”逻辑解耦，方便维护和升级。
#2.让你之后更换模型/换路径/换保存策略时，不用动其他代码，只改这一处

from __future__ import annotations
# 允许你在类型标注里写自己类名的字符串形式，避免前向引用问题
# 例如在某些情况下可以写list[Detection]而不用担心Detection还没定义
from dataclasses import dataclass
# dataclass 用来定义“纯数据结构”，让Detection这种结构更清晰，更省代码
from pathlib import Path
# Path是python里更现代，更安全的路径处理方式（相比传统的字符串路径）
from typing import List, Optional, Union
# list 用于类型标注：Optional 在当前文件里没用到，但以后可能会用到

from ultralytics import YOLO
# ultralytics 是 YOLOv8 的官方python包，封装了模型加载和推理等功能

# --------------------------
# 关键模块 1：Detection 数据结构
# --------------------------
@dataclass
class Detection:
    """
    Detection：你自定义的“工程化输出结构”。

    为什么不直接把 YOLO 的 r.boxes 往后传？
    - YOLO 的 boxes 是内部对象，含 tensor / 设备信息 / 多字段，后续处理不直观
    - 一旦 Ultralytics 版本变化，其内部字段/行为也可能变化
    - 工程上更希望输出固定字段：
      class_id / class_name / confidence / bbox
    这样后续写：
      - 二元判定
      - 通信协议（给 STM32 / 串口）
      - CSV 日志
      - GUI 显示
    都更稳定、可复现。
    """
    class_id: int # 目标类别ID
    class_name: str # 目标类别名称
    confidence: float # 置信度分数
    bbox_xyxy: List[float]  # 边界框 [x1, y1, x2, y2]（像素坐标）
"""
个人理解：Detection是一个数据类，用于存储目标检测的结果信息。它包含以下字段：
- class_id: 整数类型，表示检测到的目标的类别ID。
- class_name: 字符串类型，表示检测到的目标的类别名称。
- confidence: 浮点数类型，表示检测结果的置信度分数。
- bbox_xyxy: 浮点数列表，表示检测到的目标的边界框坐标，格式为[x1, y1, x2, y2]，分别表示边界框的左上角和右下角的坐标。
"""

# PathLike：允许函数既接受 str 也接受 Path，提高易用性
PathLike = Union[str, Path]

# --------------------------
# 关键模块 2：推理器对象（模型只加载一次）
# --------------------------
class YoloInfer:
    '''
       YoloInfer 是“推理器对象”。

    设计成 class 的好处：
    - model 只加载一次（YOLO(str(model_path)) 这一步可能比较重）
    - 你可以重复 infer_image(...) 多次而不用重复加载模型
    '''
    """
    推理器的本体，把模型加载和推理封装在一个类里。
    """
    def __init__(self, model_path: PathLike, device: Optional[Union[str, int]] = None):
        """
        初始化并加载模型。

        参数：
        - model_path：模型权重路径（如 best.pt）
        - device：推理设备
          - None：让 Ultralytics 自动选择（可能优先 GPU）
          - "cpu"：强制 CPU
          - "cuda:0" 或 0：指定 GPU
        """
        self.model_path = Path(model_path)
        if not self.model_path.exists():
            # 如果路径不对，直接报错，避免后续“跑了半天才发现没加载到模型”
            raise FileNotFoundError(f"Model not found: {self.model_path}")
        # 加载 YOLO 模型到内存
        self.model = YOLO(str(self.model_path))
         # 保存 device 配置，infer_image 时传给 predict
        self.device = device  # 例如 "cpu" / 0 / "0" / "cuda:0"

    # --------------------------
    # 关键模块 3：单张图片推理函数（核心入口）
    # --------------------------
    def infer_image(
        self,
        image_path: PathLike,
        conf: float = 0.45,
        iou: float = 0.30,
        imgsz: int = 320,
        save: bool = True,
        save_txt: bool = False,
        project: Optional[PathLike] = None,
        name: str = "api_predict",
    ) -> List[Detection]:
        """
        对单张图片执行推理，并返回 Detection 列表。

        参数解释（非常关键，后续写论文/调参就靠这段定义）：
        - conf：YOLO 内部置信度阈值（先过滤一遍候选框）
          * 注意：这不是你最终二元判定阈值（那个在 binary_decision 里）
        - iou：NMS（非极大值抑制）重叠阈值，用于“合并/抑制重复框”
          * iou 影响多框，但在“二元识别”任务里不影响 YES/NO
        - imgsz：推理时的输入尺寸（建议与训练一致，比如 320）
        - save：是否保存带框图片（验收阶段建议 True，封存后可改 False）
        - save_txt：是否输出 labels/*.txt（这是最硬的证据链，验收建议 True）
        - project/name：控制输出目录 runs 的位置与名称
        """
        img = Path(image_path)
        if not img.exists():
            raise FileNotFoundError(f"Image not found: {img}")

        # 默认输出目录：当前工作目录下的 experiments/runs
        # （你从哪个终端启动，就会在哪个目录下生成，比较符合工程习惯）
        if project is None:
            project = Path.cwd() / "experiments" / "runs"

        project = Path(project)

        # --------------------------
        # 调用 Ultralytics 推理
        # --------------------------
        results = self.model.predict(
            source=str(img), # 图片路径
            conf=conf,       #YOLO 内部置信度阈值
            iou=iou,         #NMS IoU重叠阈值
            imgsz=imgsz,     #推理输入尺寸
            device=self.device,   # None 则自动选择；你也可以强制 "cpu" 或 "cuda:0"
            save=save,        # 是否保存带框图片
            save_txt=save_txt,# 是否保存 labels/*.txt
            project=str(project), # 输出目录
            name=name,      # 子目录名称
            verbose=False,  # 关闭冗余日志
        )

        r = results[0]  # 单图推理，取第一张结果
        out: List[Detection] = [] # 把 YOLO 内部结构转换成我们自己的 Detection 列表

        # 如果没有任何检测框，直接返回空列表（后续二元判定会输出 NO）
        if r.boxes is None or len(r.boxes) == 0:
            return out
        # names：类别 id 到类别名的映射（例如 0->"insect"）
        names = self.model.names
        boxes = r.boxes
        
        # 逐个框转换成 Detection
        for i in range(len(boxes)):
            cid = int(boxes.cls[i]) # 类别 ID
            out.append(
                Detection(
                    class_id=cid,
                    class_name=str(names.get(cid, cid)), # 类别名称
                    confidence=float(boxes.conf[i]), # 置信度
                    bbox_xyxy=[float(x) for x in boxes.xyxy[i].tolist()], # 边界框坐标
                )
            )

        return out

# --------------------------
# 关键模块 4：仅用于“独立运行自测”的 main
# --------------------------
def _default_paths() -> tuple[Path, Path]:
    """
    默认路径（仅用于你直接运行 ai_infer.py 做自测）
    - 默认模型：当前目录下 yolov8n.pt（或你自己改成 best.pt）
    - 默认图片：当前目录下 test.jpg
    """
    base = Path.cwd()
    return base / "yolov8n.pt", base / "test.jpg"


if __name__ == "__main__":
    # 当你直接 python ai_infer.py 时，会走这里。
    # 这段仅用于“快速验证：推理模块是否工作正常”
    model_pt, test_img = _default_paths()

    infer = YoloInfer(model_pt, device="cpu")  # 你也可以改成 device=0 或 "cuda:0"
    dets = infer.infer_image(test_img, conf=0.25, iou=0.45, imgsz=320, save=True, save_txt=True)

    print(f"Detected objects: {len(dets)}")
    for d in dets[:10]:
        print(d)
