
"""
pip install m3u8 first, please
"""

import m3u8

m3u8_c = m3u8.load("http://192.168.7.90:20119/live/cctv1/encoder/0/playlist.m3u8")

print m3u8_c.target_duration
print m3u8_c.media_sequence
print m3u8_c.segments
