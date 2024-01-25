#!/bin/bash
wget https://developer.download.nvidia.com/compute/cuda/12.3.2/local_installers/cuda-repo-debian12-12-3-local_12.3.2-545.23.08-1_amd64.deb
sudo dpkg -i cuda-repo-debian12-12-3-local_12.3.2-545.23.08-1_amd64.deb
sudo cp /var/cuda-repo-debian12-12-3-local/cuda-*-keyring.gpg /usr/share/keyrings/
sudo add-apt-repository contrib
sudo apt-get update
sudo apt-get -y install cuda-toolkit-12-3
sudo apt-get install -y cuda-drivers
sudo apt-get install -y nvidia-kernel-open-dkms
sudo apt-get install -y cuda-drivers
