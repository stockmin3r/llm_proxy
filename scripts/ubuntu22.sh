#!/bin/bash

# This script installs CUDA toolkit and drivers on Ubuntu 22.04.

# Download and install CUDA repository pin
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-ubuntu2204.pin
mv cuda-ubuntu2204.pin /etc/apt/preferences.d/cuda-repository-pin-600

# Download and install CUDA repository package
wget https://developer.download.nvidia.com/compute/cuda/12.3.2/local_installers/cuda-repo-ubuntu2204-12-3-local_12.3.2-545.23.08-1_amd64.deb
sudo dpkg -i cuda-repo-ubuntu2204-12-3-local_12.3.2-545.23.08-1_amd64.deb

# Copy CUDA repository keyring
sudo cp /var/cuda-repo-ubuntu2204-12-3-local/cuda-*-keyring.gpg /usr/share/keyrings/

# Update package lists
sudo apt-get update

# Install CUDA toolkit
sudo apt-get -y install cuda-toolkit-12-3

# Install CUDA drivers
sudo apt-get install -y cuda-drivers

# Install NVIDIA kernel module
sudo apt-get install -y nvidia-kernel-open-545

# Install CUDA drivers for NVIDIA
sudo apt-get install -y cuda-drivers-545
