#!/bin/bash

# This script installs CUDA toolkit and NVIDIA drivers on Fedora 37.

# Download CUDA repository package
wget https://developer.download.nvidia.com/compute/cuda/12.3.2/local_installers/cuda-repo-fedora37-12-3-local-12.3.2_545.23.08-1.x86_64.rpm

# Install CUDA repository package
sudo rpm -i cuda-repo-fedora37-12-3-local-12.3.2_545.23.08-1.x86_64.rpm

# Clean the package manager cache
sudo dnf clean all

# Install CUDA toolkit
sudo dnf -y install cuda-toolkit-12-3

# Install latest NVIDIA driver using DKMS
sudo dnf -y module install nvidia-driver:latest-dkms

# Install open-source NVIDIA driver using DKMS
sudo dnf -y module install nvidia-driver:open-dkms
