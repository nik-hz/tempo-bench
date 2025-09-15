# --- Stage 1: build syfco with system GHC + cabal (small & reliable)
FROM debian:bookworm-slim AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ghc cabal-install git libgmp-dev zlib1g-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build syfco
RUN git clone --depth 1 https://github.com/reactive-systems/syfco.git /src/syfco \
    && cd /src/syfco \
    && cabal update \
    && cabal v2-install --installdir=/out --overwrite-policy=always


# ---------- Stage 2 (same as above) ----------
FROM python:3.12-bullseye

# base tools
RUN apt-get update && apt-get install -y --no-install-recommends \
      mona graphviz ca-certificates zsh curl git wget gnupg && \
    rm -rf /var/lib/apt/lists/*

# Python deps
RUN pip install --no-cache-dir Scarlet-ltl==0.0.4 ltlf2dfa==1.0.1

# oh-my-zsh + powerlevel10k
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended \
 && git clone --depth=1 https://github.com/romkatv/powerlevel10k.git \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/themes/powerlevel10k

# install Spot

RUN wget http://www.lrde.epita.fr/dload/spot/spot-2.14.1.tar.gz \
    && tar zxvf spot-2.14.1.tar.gz \
    && ./configure --prefix ~/.local && make -j"$(nproc)" && make install && \
    cd .. && rm -rf spot-2.14.1 spot-2.14.1.tar.gz

RUN wget -qO- https://www.lrde.epita.fr/repo/debian.gpg | gpg --dearmor -o /usr/share/keyrings/spot.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/spot.gpg] http://www.lrde.epita.fr/repo/debian/ bullseye/" > /etc/apt/sources.list.d/spot.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends spot libspot-dev spot-doc python3-spot \
 && rm -rf /var/lib/apt/lists/*

# bring syfco from builder
COPY --from=builder /out/syfco /usr/local/bin/syfco

WORKDIR /app
CMD [ "zsh" ]
