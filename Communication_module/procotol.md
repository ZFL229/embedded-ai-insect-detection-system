目标
在字节流(UART/USB CDC/TCP等)上可靠传输单帧JPEG。
接收端必须能做到：
1.明确找到帧边界（重同步）
2.严格按照长度接收
3.过CRC32判定传输是否损坏
4.失败时丢弃整帧并回到同步状态（不拼接，不修补）

字节序与编码
Byte order:小端（Little-endian）
所有字段均为无符号整数
Payload:JPEG原始字节序列（不做任何转码/解码）

帧结构
| Field   | Size  | Value / Meaning                          |
| ------- | ----- | ---------------------------------------- |
| SOF     | 2B    | `0xAA 0x55`（Start of Frame，同步头）          |
| VER     | 1B    | `0x01`（协议版本）                             |
| TYPE    | 1B    | `0x01` = JPEG                            |
| SEQ     | 2B    | 帧序号 `uint16`（从 0 递增，溢出回绕）                |
| LEN     | 4B    | Payload 长度 `uint32`（JPEG bytes 数）        |
| PAYLOAD | LEN B | JPEG bytes                               |
| CRC32   | 4B    | 对 **PAYLOAD** 计算 CRC32（IEEE 802.3 标准多项式） |
| EOF     | 2B    | `0x55 0xAA`（End of Frame，可用于调试与快速丢弃）     |

Header 固定长度 = 2+1+1+2+4 = 10bytes

接收端状态机（必须满足）
1.FIND_SOF:在字节流中欢动搜索'AA55'
2.READ_HEADER:读取VER/TYPE/SEQ/LEN
3.SANITY_CHECK:
    VER必须为0x01
    TYPE必须为0x01
    LEN 必须在允许范围内（例如 1 <= LEN <= 200000，你可按带宽调整）
    不满足则回到 FIND_SOF
4.READ_PAYLOAD:严格读取LEN字节（带超市）
5.READ_CRC_EOF:读取CRC32与EOF
6.VERIFY:CRC32校验失败-丢弃整帧，回到FIND_SOF
7.DELIVER/SAVE:校验成功-输出JPEG bytes(保存为文件或交给后续解码)

错误分类（用于第三阶段验收）
通信错误(Transmission Error):CRC失败/LEN不合理/超时/SOF错位
采集错误(Acquisition Error):CEC32通过，但图像内容异常（遮挡，过暗，对焦失败等）

备注（刻意的保守设计）
1.不做分片，不做并发，不做重传（先稳）
2.通过SEQ统计丢帧情况
3.后续可扩展TYPE（如RAW,控制命令，ACK/NAK）
