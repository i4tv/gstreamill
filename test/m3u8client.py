
"""
pip install m3u8 first, please
"""

import m3u8
import time
import urllib2

url = "http://localhost:20119/live/CCTV-1/encoder/0/playlist.m3u8"
media_sequence = 0

playlist = m3u8.load(url)
while playlist.is_variant:
    url = playlist.base_uri + "/" + playlist.playlists[0].uri
    playlist = m3u8.load(url)

current_sequence = media_sequence = playlist.media_sequence
for segment in playlist.segments:
    print current_sequence, segment.uri
    current_sequence += 1

while True:
    playlist = m3u8.load(url)
    if playlist.media_sequence == media_sequence:
        time.sleep(1)
        continue
    media_sequence = playlist.media_sequence
    target_duration = playlist.target_duration

    if current_sequence < media_sequence:
        print "ERROR : missing segment"
        current_sequence = media_sequence

    index = -1
    for segment in playlist.segments:
        index += 1
        if media_sequence + index < current_sequence:
            continue
        seg_url = "%s/%s" % (playlist.base_uri, segment.uri)
        response = urllib2.urlopen(seg_url)
        buf = response.read()
        f = open(segment.uri, "w")
        f.write(buf)
        f.close
        print "index: ", media_sequence + index, ", uri: ", segment.uri, ", size ", len(buf)
        current_sequence += 1

    time.sleep(target_duration - 1)
