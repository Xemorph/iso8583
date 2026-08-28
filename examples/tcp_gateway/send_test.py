#!/usr/bin/env python3
"""Test-Client für den TCP-Gateway: sendet eine kodierte ISO-8583-
Authorization-Request (DE 2,3,4,11,41,49) an den Gateway und zeigt,
was er zurückgibt.

Usage:
    python send_test.py [host] [port] [ascii|ebcdic]
    defaults: 127.0.0.1 9000 ascii
"""
import socket
import sys

# IBM-1047 (CCID), identisch zur Libiso8583-Tabelle kAsciiToEbcdic
# (aus include/iso8583/_codec.hh generiert: ASCII-Code -> EBCDIC-Byte).
# Ziffern: 0xF0-0xF9, A-Z: 0xC1-0xE9, a-z: 0x81-0xA9.
ASCII_TO_EBCDIC = {
    ord(" "): 0x40, ord("!"): 0x5A, ord('"'): 0x7F, ord("#"): 0x7B,
    ord("$"): 0x5B, ord("%"): 0x6C, ord("&"): 0x50, ord("("): 0x4D,
    ord(")"): 0x5D, ord("*"): 0x5C, ord("+"): 0x4E, ord(","): 0x6B,
    ord("-"): 0x60, ord("."): 0x4B, ord("/"): 0x61, ord("0"): 0xF0,
    ord("1"): 0xF1, ord("2"): 0xF2, ord("3"): 0xF3, ord("4"): 0xF4,
    ord("5"): 0xF5, ord("6"): 0xF6, ord("7"): 0xF7, ord("8"): 0xF8,
    ord("9"): 0xF9, ord(";"): 0x5E, ord("<"): 0x4C, ord("="): 0x7E,
    ord(">"): 0x6E, ord("?"): 0x6F, ord("@"): 0x7C, ord("A"): 0xC1,
    ord("B"): 0xC2, ord("C"): 0xC3, ord("D"): 0xC4, ord("E"): 0xC5,
    ord("F"): 0xC6, ord("G"): 0xC7, ord("H"): 0xC8, ord("I"): 0xC9,
    ord("J"): 0xD1, ord("K"): 0xD2, ord("L"): 0xD3, ord("M"): 0xD4,
    ord("N"): 0xD5, ord("O"): 0xD6, ord("P"): 0xD7, ord("Q"): 0xD8,
    ord("R"): 0xD9, ord("S"): 0xE2, ord("T"): 0xE3, ord("U"): 0xE4,
    ord("V"): 0xE5, ord("W"): 0xE6, ord("X"): 0xE7, ord("Y"): 0xE8,
    ord("Z"): 0xE9, ord("_"): 0x6D, ord("a"): 0x81, ord("b"): 0x82,
    ord("c"): 0x83, ord("d"): 0x84, ord("e"): 0x85, ord("f"): 0x86,
    ord("g"): 0x87, ord("h"): 0x88, ord("i"): 0x89, ord("j"): 0x91,
    ord("k"): 0x92, ord("l"): 0x93, ord("m"): 0x94, ord("n"): 0x95,
    ord("o"): 0x96, ord("p"): 0x97, ord("q"): 0x98, ord("r"): 0x99,
    ord("s"): 0xA2, ord("t"): 0xA3, ord("u"): 0xA4, ord("v"): 0xA5,
    ord("w"): 0xA6, ord("x"): 0xA7, ord("y"): 0xA8, ord("z"): 0xA9,
    ord("|"): 0x4F,
}


def to_wire(data: str, encoding: str) -> bytes:
    if encoding == "ebcdic":
        out = bytearray()
        for ch in data:
            if ord(ch) not in ASCII_TO_EBCDIC:
                raise ValueError("Zeichen %r nicht in der IBM-1047-Tabelle" % ch)
            out.append(ASCII_TO_EBCDIC[ord(ch)])
        return bytes(out)
    return data.encode("ascii")


def build_frame(encoding: str) -> bytes:
    # identisch zu tests/test_e2e_full_message.cc (ASCII-Setup):
    des = [2, 3, 4, 11, 41, 49]
    # exakt wie iso8583::utils::makeBitmap(): MSB-first pro Byte,
    # p = de - 1, byte = p // 8, bit = 7 - p % 8
    bmp = bytearray(8)
    for de in des:
        p = de - 1
        bmp[p // 8] |= 1 << (7 - p % 8)
    bitmap = bytes(bmp)

    def enc(s: str) -> bytes:
        return to_wire(s, encoding)

    frame = bytearray()
    frame += enc("0100")                 # MTI: Authorization Request
    frame += bitmap                      # bitmap ist encodingsunabhängig (raw bytes)
    frame += enc("16") + enc("4111111111111111")   # DE002 PAN
    frame += enc("000000")               # DE003 Processing Code
    frame += enc("000000012345")         # DE004 Amount
    frame += enc("000042")               # DE011 STAN
    frame += enc("09") + enc("TERMINAL1")  # DE041 Terminal-ID (LL, 9 Zeichen)
    frame += enc("978")                  # DE049 Currency EUR
    return bytes(frame)


def main() -> None:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
    encoding = sys.argv[3] if len(sys.argv) > 3 else "ascii"
    if encoding not in ("ascii", "ebcdic"):
        sys.exit(f"unknown encoding: {encoding}")

    frame = build_frame(encoding)
    print(f"SENDING {len(frame)} bytes ({encoding}) to {host}:{port}")

    with socket.create_connection((host, port), timeout=10) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.sendall(frame)
        s.shutdown(socket.SHUT_WR)  # Frame fertig -> Gateway liest bis EOF/Timeout
        try:
            data = s.recv(65536)
        except OSError:
            data = b""
    sys.stdout.write(data.decode("utf-8", "replace"))


if __name__ == "__main__":
    main()