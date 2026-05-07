# binary_decision.py
# 有虫 = 1，无虫 = 0
#
# 这个文件的意义非常大：
# - 你最终要交给嵌入式/通信/显示屏的不是“检测框”，而是一个稳定决策：0/1
# - YOLO 输出再怎么复杂、多框、抖动，只要 ai_infer.py 输出的 Detection 结构稳定，
#   这里的二元判定逻辑就永远不需要改（工程解耦）

from __future__ import annotations
from dataclasses import dataclass
from typing import Iterable, Tuple

# 这里复用 ai_infer.py 的 Detection 数据结构
# 好处：所有模块都围绕同一种 Detection 结构协作
from Ai_module.ai_infer import Detection
# --------------------------
# 关键模块 1：可解释的决策结果结构（用于调试/验收/论文）
# --------------------------
@dataclass
class BinaryDecision:
    """
    BinaryDecision 是“调试/解释用途”的结构体（可选）。

    注意：
    - 最终给嵌入式输出只需要 0/1
    - 但在开发、调参、写论文时，你必须能回答：
      “为什么这张图被判 YES/NO？”
    所以保留这个结构体能显著提升可追溯性。
    """
    y: int                 # 0/1
    reason: str     # 解释原因（便于日志、论文表述、调参）
    max_confidence: float   # 检测结果里最高的置信度（辅助判断）
    matched_count: int      # 满足条件的检测框数量
"""
作用：提供一个“可解释的决策结果容器”。
它不是必须的（你完全可以只返回 0/1），但它有两个工程价值：
可记录：日志、实验记录、论文里的“判定依据”可以直接存这个结构。
可扩展：未来你想加入更多字段（比如时间戳、图片名、阈值配置、统计信息），不用改变整个系统的返回格式。
你现在版本里，它更像是“预留的工程接口”，帮助你以后把 debug 信息标准化。
"""
# --------------------------
# 关键模块 2：最核心接口（稳定输出 0/1）
# --------------------------
def binary_insect_decision(
    detections: Iterable[Detection],
    insect_class_names: Tuple[str, ...],
    conf_threshold: float = 0.45,
    min_count: int = 1,
) -> int:
    """
    输入：Detection 列表
    输出：0/1（最终你给嵌入式/系统的结果）

    规则：
    - 统计“类别命中 + 置信度 >= conf_threshold”的框数量 matched_count
    - 当 matched_count >= min_count，输出 1（YES）
    - 否则输出 0（NO）

    为什么要有 min_count？
    - 允许你对抗偶发误检：比如要求至少 2 个强框才算 YES
    - 但在第一阶段（存在性检测）通常 min_count=1 即可
    """
    matched_count = 0
    for d in detections:
        # d.class_name：例如 "person"
        # d.confidence：例如 0.87
        if d.class_name in insect_class_names and d.confidence >= conf_threshold:
            matched_count += 1
    # 关键点：必须 return
    # 你之前遇到的 None，就是因为这里忘了 return
    return 1 if matched_count >= min_count else 0
"""
作用：提供最核心、最稳定的输出接口——只输出 0 或 1。
这是你最终给嵌入式/通信/显示屏要用的那条接口：
上游喂进来一堆检测结果
这里做一次简单规则判断
直接输出 0/1
工程意义：把复杂的 AI 输出“压缩”为单 bit 决策，降低后续系统复杂度，也更稳定。
"""
# --------------------------
# 关键模块 3：调试/验收接口（返回可解释信息）
# --------------------------
def binary_insect_decision_debug(
    detections: Iterable[Detection],
    insect_class_names: Tuple[str, ...] = ("insect",),
    conf_threshold: float = 0.45,
    min_count: int = 1,
) -> BinaryDecision:
    """
    输入：Detection 列表
    输出：BinaryDecision（y + reason + max_conf + matched_count）

    用途：
    - 阶段验收：生成 CSV 记录，每张图为什么 YES/NO 都能解释
    - 调参：你能分清“完全没检出” vs “检出了但置信度不够”
    - 写论文：可解释性证据
    """
    det_list = list(detections)  # iterable 可能只能遍历一次，先转 list 最稳

    matched = [
        d for d in det_list
        if d.class_name in insect_class_names and d.confidence >= conf_threshold
    ]
    # 二元输出
    y = 1 if len(matched) >= min_count else 0
    # max_conf：无论类别如何，本图中最高置信度（用于解释 NO）
    max_conf = max((d.confidence for d in det_list), default=0.0)
    # 解释原因
    if y == 1:
        reason = f"YES: matched={len(matched)} >= {min_count}, conf>={conf_threshold}"
    else:
        reason = f"NO: matched={len(matched)} < {min_count}, max_conf={max_conf:.3f}, conf>={conf_threshold}"

    return BinaryDecision(
        y=y,
        reason=reason,
        max_confidence=max_conf,
        matched_count=len(matched),
    )
"""
作用：提供“可解释版本”的同一条决策逻辑。
它做的决策与上面那个函数一致，但额外返回一段文本说明，用于：
你调阈值时快速判断：是“检测到了但不够强”，还是“完全没检测到”
你写周总结/论文时能更容易解释：为什么判 1/0
工程意义：同样的判定逻辑，给出人可读的解释，用于调试与可追溯性。
"""
                                  