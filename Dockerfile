From ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest

USER root

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3-pip \
    sudo \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /home/root
WORKDIR /home/root

CMD [ "/bin/bash" ]