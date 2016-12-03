
from __future__ import print_function
import sys
import csv

def pipeStalls(data):
  return float(data["Issue Stall Reasons (Pipe Busy)(%)"])

def relativeFreq(data):
  fp32 = float(data["FP Instructions(Single)"])
  fp64 = float(data["FP Instructions(Double)"])
  trans = float(data["Floating Point Operations(Single Precision Special)"])
  intops = float(data["Integer Instructions"])
  conv = float(data["Bit-Convert Instructions"])
  warp = float(data["Inter-Thread Instructions"])
  logic = float(data["Control-Flow Instructions"])
  misc = float(data["Misc Instructions"])

  total = fp32 + fp64 + trans + intops + conv + warp + logic + misc
  fp32 = fp32/total
  fp64 = fp64/total
  trans = trans/total
  intops = intops/total
  conv = conv/total
  warp = warp/total
  logic = logic/total
  misc = misc/total

  print("FP32: " + str(fp32))
  print("FP64: " + str(fp64))
  print("Trans: " + str(trans))
  print("IntOps: " + str(intops))
  print("Conv: " + str(conv))
  print("Warp: " + str(warp))
  print("Logic: " + str(logic))
  print("Misc: " + str(misc))

with open(sys.argv[1]) as f:
  csvf = list(csv.reader(f, delimiter=',', quotechar="\""))
  data = {}
  for i in range(0,len(csvf[0])):
    data[csvf[0][i]]=csvf[1][i]
  if(pipeStalls(data) > 5):
      print(sys.argv[1] + ": " + str(pipeStalls(data))+"% pipeline stalls")
      relativeFreq(data)
      print()

