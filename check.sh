psql -X -a -q postgres -f sql/zhparser.sql | diff expected/zhparser.out -

if [ $? -eq 0 ]
then
    echo "pass!"
else
    echo "do not pass!"
fi
