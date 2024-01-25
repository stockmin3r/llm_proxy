#!/bin/bash
wget https://developer.download.nvidia.com/compute/cuda/12.3.2/local_installers/cuda-repo-fedora37-12-3-local-12.3.2_545.23.08-1.x86_64.rpm
sudo rpm -i cuda-repo-fedora37-12-3-local-12.3.2_545.23.08-1.x86_64.rpm
sudo dnf clean all
sudo dnf -y install cuda-toolkit-12-3
sudo dnf -y module install nvidia-driver:latest-dkms
sudo dnf -y module install nvidia-driver:open-dkms
