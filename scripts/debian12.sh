#!/bin/bash

# This script installs CUDA 12.3.2 on Debian 12.

# Download CUDA repository package
wget https://developer.download.nvidia.com/compute/cuda/12.3.2/local_installers/cuda-repo-debian12-12-3-local_12.3.2-545.23.08-1_amd64.deb

# Install CUDA repository package
sudo dpkg -i cuda-repo-debian12-12-3-local_12.3.2-545.23.08-1_amd64.deb

# Copy CUDA keyring to system keyrings directory
sudo cp /var/cuda-repo-debian12-12-3-local/cuda-*-keyring.gpg /usr/share/keyrings/

# Add contrib repository
sudo add-apt-repository contrib

# Update package lists
sudo apt-get update

# Install CUDA toolkit
sudo apt-get -y install cuda-toolkit-12-3

# Install CUDA drivers
sudo apt-get install -y cuda-drivers

# Install NVIDIA kernel module
sudo apt-get install -y nvidia-kernel-open-dkms

# Install CUDA drivers again
sudo apt-get install -y cuda-drivers
