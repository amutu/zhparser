pid=$$
docker run --rm --name testpgzhparser-$pid -p 5432:5432 -d -e POSTGRES_PASSWORD=somepassword@debian-16 zhparser/zhparser:bookworm-16
sleep 5
export PGPASSWORD=somepassword@debian-16
psql -h 127.0.0.1 -X -a -q postgres postgres -f sql/zhparser.sql | diff expected/zhparser-debian.out -

if [ $? -eq 0 ]
then
    echo "pass!"
else
    echo "do not pass!"
fi
docker stop testpgzhparser-$pid
