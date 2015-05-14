
./iris_mysql -d prepaydb -L ./irisLog -s 0.0.0.0 -p 44335 -S 127.0.0.1 -P 20301 -i localhost -o 3306 -u prepay -U 9repay -r -n -H -T

sleep 5

echo -e "\n\n\r******************** RESTARTING PREPAY ****************\n\r\n\n" >> ./irisLog
. ./start_server.sh

