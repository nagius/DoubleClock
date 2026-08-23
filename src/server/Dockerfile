FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

# Install PHP CLI, timezone data, and basic utilities commonly needed
# by the update/server scripts.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        php-cli \
        tzdata \
        wget \
    && rm -rf /var/lib/apt/lists/*

# Create the unprivileged service user and its home directory.
RUN useradd --create-home --shell /bin/sh timezoned

WORKDIR /home/timezoned

# Copy the application scripts into the timezoned user's home.
COPY update server /home/timezoned/

# Make sure the scripts belong to the service user.
RUN chown timezoned:timezoned /home/timezoned/update /home/timezoned/server \
    && chmod +x /home/timezoned/update \
    && chmod +x /home/timezoned/server \
    \
    # Replace the first "#!" line with the system PHP binary.
    && sed -i '1c\#!/usr/bin/php' /home/timezoned/server

# Run the timezone update as the service user during the image build.
USER timezoned
RUN /home/timezoned/update

# The server listens for UDP packets on port 2342.
EXPOSE 2342/udp

# Run the server as the unprivileged user.
CMD ["/home/timezoned/server"]
