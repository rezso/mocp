#cd "$2"
~/bin/ratings_find $1 "$2" > /tmp/pl.m3u
mocp -cap /tmp/pl.m3u&
