Example:

 $ docker build --tag kgiusti/router1:1 --file Dockerfile-router1 --build-arg proton_branch=0.20.0 --build-arg dispatch_branch=1.0.1 .
 $ docker build --tag kgiusti/router2:1 --file Dockerfile-router2 --build-arg proton_branch=0.20.0 --build-arg dispatch_branch=1.0.1 .
 $ docker run -d --name Router1-2router --net=host kgiusti/router1:1
 $ docker run -d --name Router2-2router --net=host kgiusti/router2:1 
