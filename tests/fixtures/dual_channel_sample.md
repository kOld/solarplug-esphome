# Synthetic dual-channel fixture for parser tests.
# Values are representative protocol examples and are not a user site export.

SNIFF ts=1000 end=1018 dur=18 ch=A idx=0 len=5 reason=cr hex="48 53 54 53 0D" ascii="HSTS\r"
SNIFF ts=1018 end=1048 dur=30 ch=B idx=1 len=40 reason=cr hex="28 30 30 20 4C 30 31 30 30 30 30 30 30 30 30 30 30 20 31 31 32 31 31 30 30 31 30 30 30 4C 31 31 32 30 30 30 30 30 30 0D" ascii="(00 L010000000000 11211001000L112000000\r"

SNIFF ts=1100 end=1118 dur=18 ch=A idx=2 len=6 reason=cr hex="48 47 52 49 44 0D" ascii="HGRID\r"
SNIFF ts=1118 end=1168 dur=50 ch=B idx=3 len=50 reason=cr hex="28 32 33 36 2E 34 20 35 30 2E 30 20 32 38 30 20 30 39 30 20 37 30 20 34 30 20 2B 30 31 37 34 31 20 30 20 30 36 35 30 30 20 31 31 2B 30 30 30 30 30 0D" ascii="(236.4 50.0 280 090 70 40 +01741 0 06500 11+00000\r"

SNIFF ts=1200 end=1218 dur=18 ch=A idx=4 len=4 reason=cr hex="48 4F 50 0D" ascii="HOP\r"
SNIFF ts=1218 end=1268 dur=50 ch=B idx=5 len=50 reason=cr hex="28 32 33 36 2E 35 20 35 30 2E 30 20 30 31 35 38 34 20 30 31 35 38 35 20 30 32 34 20 30 30 30 20 30 36 32 30 30 20 30 30 35 2E 38 20 30 30 30 39 33 0D" ascii="(236.5 50.0 01584 01585 024 000 06200 005.8 00093\r"

SNIFF ts=1300 end=1318 dur=18 ch=A idx=6 len=5 reason=cr hex="48 42 41 54 0D" ascii="HBAT\r"
SNIFF ts=1318 end=1368 dur=50 ch=B idx=7 len=50 reason=cr hex="28 30 34 20 30 35 33 2E 35 20 30 38 30 20 30 30 32 20 30 30 30 30 30 20 33 39 34 20 31 30 31 30 30 32 30 31 30 30 30 30 20 30 30 30 30 30 30 30 30 0D" ascii="(04 053.5 080 002 00000 394 101002010000 00000000\r"

SNIFF ts=1400 end=1418 dur=18 ch=A idx=8 len=4 reason=cr hex="48 50 56 0D" ascii="HPV\r"
SNIFF ts=1418 end=1468 dur=50 ch=B idx=9 len=50 reason=cr hex="28 30 30 30 2E 30 20 30 30 2E 30 20 30 30 30 30 30 20 30 30 30 30 30 2E 30 20 30 30 30 30 30 20 30 20 30 36 30 2E 30 20 30 32 37 20 30 38 35 30 30 0D" ascii="(000.0 00.0 00000 00000.0 00000 0 060.0 027 08500\r"

SNIFF ts=1500 end=1518 dur=18 ch=A idx=10 len=5 reason=cr hex="48 47 45 4E 0D" ascii="HGEN\r"
SNIFF ts=1518 end=1578 dur=60 ch=B idx=11 len=60 reason=cr hex="28 32 36 30 34 32 39 20 32 30 3A 30 36 20 30 33 2E 30 34 33 20 30 30 35 39 2E 34 20 30 30 36 36 2E 34 20 30 30 30 30 30 30 30 36 36 2E 34 20 30 30 30 30 30 30 30 30 30 30 30 30 0D" ascii="(260429 20:06 03.043 0059.4 0066.4 000000066.4 000000000000\r"

SNIFF ts=1600 end=1618 dur=18 ch=A idx=12 len=6 reason=cr hex="51 50 52 54 4C 0D" ascii="QPRTL\r"
SNIFF ts=1618 end=1628 dur=10 ch=B idx=13 len=10 reason=cr hex="28 48 50 56 49 4E 56 30 32 0D" ascii="(HPVINV02\r"

SNIFF ts=1700 end=1718 dur=18 ch=A idx=14 len=7 reason=cr hex="48 49 4D 53 47 31 0D" ascii="HIMSG1\r"
SNIFF ts=1718 end=1739 dur=21 ch=B idx=15 len=21 reason=cr hex="28 30 30 34 30 2E 30 35 20 32 30 32 35 30 39 32 33 20 31 32 0D" ascii="(0040.05 20250923 12\r"
