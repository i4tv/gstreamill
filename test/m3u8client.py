
"""
pip install m3u8 first, please
"""

import sys
import m3u8
import time
import urllib2
import os

class Player:
    def __init__(self, url):
        self.url = url
        try:
            playlist = m3u8.load(url)
            while playlist.is_variant: # multiple bitrates videos
                self.url = playlist.base_uri + playlist.playlists[0].uri
                playlist = m3u8.load(self.url)
        except:
            raise NameError("OpenError")
        else:
            self.media_seq = playlist.media_sequence
            self.current_seq = playlist.media_sequence
            for segment in playlist.segments:
                print self.current_seq, segment.uri
                self.current_seq += 1

    def play(self):
        try:
            while True:
                playlist = m3u8.load(self.url)
                if playlist.media_sequence == self.media_seq:
                    time.sleep(1)
                    continue
                self.media_seq = playlist.media_sequence
                target_duration = playlist.target_duration

                if self.current_seq < self.media_seq:
                    print "ERROR : missing segment"
                    self.current_seq = self.media_seq

                index = -1
                for segment in playlist.segments:
                    index += 1
                    if self.media_seq + index < self.current_seq:
                        continue
                    seg_url = "%s%s" % (playlist.base_uri, segment.uri)
                    response = urllib2.urlopen(seg_url)
                    buf = response.read()
                    if not os.path.isdir(os.path.dirname(segment.uri)):
                        os.mkdir(os.path.dirname(segment.uri))
                    f = open(segment.uri, "w")
                    f.write(buf)
                    f.close
                    print "index: ", self.media_seq + index, ", uri: ", segment.uri, ", size ", len(buf)
                    self.current_seq += 1

                time.sleep(target_duration - 1)
        except:
            print "Error load m3u8 playlist: %s" % self.url

if __name__ == "__main__":
    url = sys.argv[1]
    n = 0
    while True:
        n += 1
        print "replay %d times" % n
        try:
            player = Player(url)
        except:
            print "Open %s error" % url
            time.sleep(5)
            continue
        player.play()
        time.sleep(1)
