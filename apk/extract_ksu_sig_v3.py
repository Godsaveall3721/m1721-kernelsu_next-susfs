import struct
import hashlib
import sys

def parse_certificate_from_signer(signer_data):
    # 根据 Android apksig 规范，Signer 结构中包含 certificates 序列
    # signers -> signer -> signed data -> certificates -> certificate
    # 这里用鲁棒的字节扫描方式直接在 signer 数据中抓取 X.509 证书的特征（以 30 82 开头的 ASN.1 DER 序列）
    for i in range(len(signer_data) - 4):
        # 寻找常见的 X.509 证书前缀：SEQUENCE (0x30), 长度标识 (0x82 或 0x81)
        if signer_data[i] == 0x30 and signer_data[i+1] in (0x81, 0x82):
            if signer_data[i+1] == 0x82:
                cert_len = struct.unpack(">H", signer_data[i+2:i+4])[0] + 4
            else:
                cert_len = signer_data[i+2] + 3
            
            if 200 < cert_len < 2000: # 标准 APK 签名证书通常在数百到一千多字节之间
                cert_bytes = signer_data[i:i+cert_len]
                # 验证是否能通过 sha256 成功计算
                return cert_bytes
    return None

def analyze_apk_signature(apk_path):
    with open(apk_path, "rb") as f:
        content = f.read()

    magic = b'APK Sig Block 42'
    magic_idx = content.rfind(magic)
    
    if magic_idx == -1:
        print("[-] Error: APK Signing Block magic not found.")
        return

    footer_start = magic_idx - 8
    size_of_block = struct.unpack("<Q", content[footer_start:footer_start+8])[0]
    block_start = magic_idx - size_of_block
    
    pos = block_start + 8
    block_end = footer_start

    print(f"[+] Found APK Signing Block at offset {block_start}, size {size_of_block}")

    target_cert = None

    while pos < block_end:
        pair_size = struct.unpack("<Q", content[pos:pos+8])[0]
        pair_id = struct.unpack("<I", content[pos+8:pos+12])[0]
        val_data = content[pos+12 : pos+8+pair_size]
        
        print(f"    -> Block ID found: 0x{pair_id:08x} (Size: {pair_size})")

        # 检查 V2 (0x7109871a) 或 V3 / V3.1 (0xf05368c0 或其他高版本 ID)
        if pair_id in (0x7109871a, 0xf05368c0, 0x000006ca):
            target_cert = parse_certificate_from_signer(val_data)
            if target_cert:
                print(f"[+] Successfully extracted certificate from block ID 0x{pair_id:08x}!")
                break

        pos += 8 + pair_size

    if not target_cert:
        # 兜底方案：如果从签名块没抠出来，直接对整个 APK 的部分区块或由其他方式获取，但通常上面能直接命中
        print("[-] Error: Could not extract certificate from signing blocks.")
        return

    cert_size = len(target_cert)
    cert_hash = hashlib.sha256(target_cert).hexdigest()

    print("\n--------------------------------------------------")
    print(f"KSU_NEXT_MANAGER_SIZE := 0x{cert_size:x} ({cert_size})")
    print(f"KSU_NEXT_MANAGER_HASH := {cert_hash}")
    print("--------------------------------------------------")

if __name__ == "__main__":
    apk_file = sys.argv[1] if len(sys.argv) > 1 else "KernelSU_Next_v3.0.0_32857-release.apk"
    analyze_apk_signature(apk_file)
