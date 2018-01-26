# **********************************************************************
# Install path configuration
# **********************************************************************
# PREFIX = /path/to/libmp
# **********************************************************************
PREFIX = $(HOME)/libmp

# **********************************************************************
# LibGDSync configuration
# **********************************************************************
# GDSYNC_BUILD = 1 build the libgdsync submodule
# GDSYNC_BUILD = 0 otherwise
# ----------------------------------------------------------------------
# GDSYNC_ENABLE = /path/to/libgdsync to enable LibGDSync inside LibMP
# GDSYNC_ENABLE = "no" otherwise
# **********************************************************************
GDSYNC_BUILD = 1
GDSYNC_ENABLE = $(HOME)/libmp

# **********************************************************************
# GPUDirect RDMA Copy library configuration
# **********************************************************************
# GDRCOPY_BUILD = 1 to build the gdrcopy submodule
# GDRCOPY_BUILD = 0 otherwise
# **********************************************************************
GDRCOPY_BUILD = 1

# **********************************************************************
# CUDA configuration
# **********************************************************************
# CUDA_ENABLE = /path/to/cuda to enable CUDA inside LibMP
# CUDA_ENABLE = "no" otherwise
# **********************************************************************
CUDA_ENABLE = /usr/local/cuda-9.0

# **********************************************************************
# MPI configuration
# **********************************************************************
# MPI_ENABLE = /path/to/mpi to use MPI as TL in LibMP
# N.B. Currently libmp support only MPI as TL!
# **********************************************************************
MPI_ENABLE = /opt/openmpi/v1.10.2/cuda7.5