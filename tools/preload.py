
"""
pip install m3u8 first, please
"""

import sys
import m3u8
import time
import urllib2

class Player:
    def __init__(self, url):
        self.url = url

    def play(self):
        try:
            print "preloading", self.url
            playlist = m3u8.load(self.url)
            if playlist.is_variant: # multiple bitrates videos
                print "multiple bitrates video found"
                for subplaylist in playlist.playlists:
                    print "preloading", playlist.base_uri + subplaylist.uri
                    p = m3u8.load(playlist.base_uri + subplaylist.uri)
                    for segment in p.segments:
                        seg_url = "%s%s" % (p.base_uri, segment.uri)
                        response = urllib2.urlopen(seg_url)
                        buf = response.read()
                        print "download segment:", segment.uri
            else:
                for segment in p.segments:
                    seg_url = "%s%s" % (playlist.base_uri, segment.uri)
                    response = urllib2.urlopen(seg_url)
                    buf = response.read()
                    print "download segment: ", segment.uri, ", size ", len(buf)

        except:
            print "Error load m3u8 playlist: %s" % self.url

if __name__ == "__main__":
    url = sys.argv[1]
    try:
        player = Player(url)
    except:
        print "Open %s error" % url
    player.play()
