#!/bin/bash
for f in $( ls rodinia ); do
  for k in $( ls rodinia/$f ); do
    python ./inspect.py rodinia/$f/$k
  done
done
