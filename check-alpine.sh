export PGPASSWORD=somepassword
psql -h 127.0.0.1 -X -a -q postgres postgres -f sql/zhparser.sql | diff expected/zhparser-alpine.out -

if [ $? -eq 0 ]
then
    echo "pass!"
else
    echo "do not pass!"
fi
