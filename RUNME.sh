#!/bin/bash

BASE_DIR=$(pwd)
BASE_URL="https://houseofgraphs.org/data/cubics/"

COMPRESSED_DATASETS="cub22.g6.gz Generated_graphs.24.03.g6.gz"
DATASET_LIST="cub12.g6 cub14.g6 cub16.g6 cub18.g6 cub20.g6"

setup () {
  mkdir -pv data

  echo "Downloading datasets"
  for dataset in ${DATASET_LIST} ${COMPRESSED_DATASETS}; do
    if [ -f "${BASE_DIR}/data/${dataset}" ]; then
      echo "  dataset ${dataset} found"
    else
      echo "  downloading ${dataset}"
      wget --quiet --directory-prefix=${BASE_DIR}/data ${BASE_URL}${dataset} 
    fi
  done

  echo ""
  echo "Uncompressing datasets"
  for compressed_dataset in ${COMPRESSED_DATASETS}; do
    uncompressed_name=$(echo ${compressed_dataset} | sed 's/\.gz//')
    if [ -f "${BASE_DIR}/data/${uncompressed_name}" ]; then
      echo "  dataset ${uncompressed_name} found"
    else
      echo "  uncompressing ${compressed_dataset}"
      gunzip -k ${BASE_DIR}/data/${compressed_dataset}
    fi
  done
  cp data/Generated_graphs.24.03.g6 data/cub24.g6

  echo ""
  echo "Checking datasets integrity"
  sha256sum -c datasets.sha256
}

build () {
  mkdir -pv bin

  gcc coarse.c -o bin/sequencial
  gcc coarse.c -fopenmp -o bin/coarse
  gcc main.c -fopenmp -o bin/fine
}


experiment () {
  rm -rf results
  mkdir -v results

  for bin in sequencial coarse fine; do
    OUTPUT_FILE="results/${bin}-output.txt"
    if [[ -f ${OUTPUT_FILE} ]]; then
      rm -fv ${OUTPUT_FILE}
    fi
    for size in 12 14 16 18 20; do
      dataset="cub${size}.g6"
      echo ""
      echo "Running ${bin} with ${dataset}"
      RUN_TIME=$(/usr/bin/time -f "%e" ./bin/${bin} data/${dataset} 2>&1 >/dev/null)
      echo "(${size},${RUN_TIME})" >> ${OUTPUT_FILE}
    done
  done

  # Do not run 'fine' with large instances
  for bin in sequencial coarse; do
    OUTPUT_FILE="results/${bin}-output.txt"
    for size in 22 24; do
      dataset="cub${size}.g6"
      echo ""
      echo "Running ${bin} with ${dataset}"
      RUN_TIME=$(/usr/bin/time -f "%e" ./bin/${bin} data/${dataset} 2>&1 >/dev/null)
      echo "(${size},${RUN_TIME})" >> ${OUTPUT_FILE}
    done
  done
}

main () {
  echo "Running setup"
  setup
  echo "Running build"
  build
  echo "Running experiment"
  experiment
}
main
