pid=$$
docker run --rm --name testpgzhparser-$pid -p 5432:5432 -d -e POSTGRES_PASSWORD=somepassword@alpine zhparser/zhparser:alpine-16
sleep 5
export PGPASSWORD=somepassword@alpine
psql -h 127.0.0.1 -X -a -q postgres postgres -f sql/zhparser.sql | diff expected/zhparser-alpine.out -

if [ $? -eq 0 ]
then
    echo "pass!"
else
    echo "do not pass!"
fi
docker stop testpgzhparser-$pid
