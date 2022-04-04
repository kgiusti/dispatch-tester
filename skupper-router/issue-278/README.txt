To reproduce:
Create two routers, one with a tcpListener, and one with a tcpConnector
Establish two services - one for the tcpConnector and one for a tcpConnector you will create dynamically, example:
  $ podman run --rm --name nginx-http1-00 -d -p 8800:80 nginx-perf:1
  $ podman run --rm --name nginx-http1-01 -d -p 8801:80 nginx-perf:1
  
Run a curl client that repeatedly GETs a large HTML document (in my case 10MB):
   while true; do curl --max-time 60000 127.0.0.1:8000/t10M.html > /dev/null ; done

In another window, repeatedly create/delete a new tcpConnector for the second service:
  $ skmanage CREATE -b 127.0.0.1:5673 --type io.skupper.router.tcpConnector --name "tcpConnector/0.0.0.0:8801" address=closest/tcp-bridge port=8801 host=127.0.0.1
  $ skmanage DELETE -b 127.0.0.1:5673 --type io.skupper.router.tcpConnector --name "tcpConnector/0.0.0.0:8801"
  
  This should cause the router to crash as described in the issue.
