__global__ void k(int *ret) {
  for(int i=0; i<100000; i++) {
    *ret = i;
  }
}

int main(int argc, char** argv) {
  int *d_argc;
  cudaMalloc(&d_argc, sizeof(int));
  cudaMemcpy(d_argc, &argc, sizeof(int), cudaMemcpyHostToDevice);
  k<<<1,1>>>(d_argc);
  for(int i=0; i<100; i++) {
    argc += i;
  }
}
