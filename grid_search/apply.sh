for i in {1..19}; do
        NODE="node$i"
        scp ~/distComp/conf.json $NODE:~/distComp/conf.json
done
