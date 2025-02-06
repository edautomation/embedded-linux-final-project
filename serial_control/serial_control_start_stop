#! /bin/sh

case $1 in 
    start)
        echo "Starting serial control daemon"
        start-stop-daemon -S -n serial-control -a /usr/bin/serial-control -- -d
        ;;
    stop)
        echo "Stopping serial control daemon"
        start-stop-daemon -K -n serial-control
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0

