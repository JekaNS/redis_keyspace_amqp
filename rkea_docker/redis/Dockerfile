FROM grokzen/redis-cluster:5.0.5

ARG SSH_PORT=2222

RUN apt-get update
RUN LC_ALL=C.UTF-8 apt-get install -qqy apt-utils
RUN LC_ALL=C.UTF-8 apt-get install -qqy software-properties-common curl dialog aptitude mc iputils-ping net-tools vim git zip unzip
RUN LC_ALL=C.UTF-8 apt-get install -qqy openssh-server build-essential cmake gdb librabbitmq-dev

RUN mkdir /root/.ssh
RUN chmod 0700 /root/.ssh
COPY ssh_host_rsa_key.pub /root/.ssh/authorized_keys
COPY ssh_host_rsa_key /etc/ssh/ssh_host_rsa_key
RUN chmod 0600 /root/.ssh/authorized_keys
COPY sshd_config /etc/ssh/sshd_config

RUN sed -i "/^Port 2222/c\Port $SSH_PORT" /etc/ssh/sshd_config
RUN sed -i "/^#!\/bin\/sh$/c\#!\/bin\/sh\n/etc/init.d/ssh start\n" /docker-entrypoint.sh

EXPOSE $SSH_PORT

