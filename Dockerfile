# Official SYNTCOMP Docker container for OmegaSynth
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY src/ /app/src/
COPY Makefile /app/Makefile
COPY syntcomp_run.sh /app/syntcomp_run.sh

RUN make clean && make
RUN chmod +x /app/syntcomp_run.sh

ENTRYPOINT ["/app/syntcomp_run.sh"]
