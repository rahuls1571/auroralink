docker run -d -v "$(pwd)/EGD_OUT.xml:/tmp/server/EGD" --entrypoint="python3" -w /tmp/server -p 7939:7938 python:3  -m http.server 7938
