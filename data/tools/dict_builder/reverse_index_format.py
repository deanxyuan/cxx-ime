"""Current binary layout shared by the reverse-index builder and verifier."""

import struct


REVERSE_INDEX_MAGIC = b"CXRIDX\x00\x00"
REVERSE_INDEX_VERSION = 1
REVERSE_INDEX_HEADER_FORMAT = "<8s7I"
REVERSE_INDEX_HEADER_SIZE = struct.calcsize(REVERSE_INDEX_HEADER_FORMAT)
