#!/bin/bash

TIMEOUT=60s

SCRIPT_DIR=$(cd $(dirname $0); pwd)
TEST_DIR=$SCRIPT_DIR/test_tmp

INFILE=$TEST_DIR/sockimux_infile

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

make_infile () {
  for i in $(seq 50)
  do
    cat $(for j  in $(seq 30); do echo ./sockimux; done) >> $1
  done
}

connect_socks () {
  mysockA=$1
  mysockB=$2
  mynum_conns=$3
  for i in $(seq $mynum_conns)
  do
    socat UNIX-CONNECT:$mysockA UNIX-CONNECT:$mysockB &
    mysocat_pids="$mysocat_pids $!"
  done
  echo $mysocat_pids
}

cd $(dirname $0)

if [ ! -f ./sockimux ]
then
  echo First compile "sockimux" by running "make"
  exit 1
fi

if [ -e $TEST_DIR ]
then
  if [ ! -d "$TEST_DIR" ]
  then
    echo Cannot create test directory: $TEST_DIR already exists and is a file
  fi
else
  mkdir $TEST_DIR
fi

if [ ! -f "$INFILE" ]
then
  make_infile $INFILE
fi

run_test () {
  infile=$1
  num_streams=$2
  desc=$3

  test_hash=$RANDOM
  imuxAsock=$TEST_DIR/imuxA.sock.$test_hash
  imuxBsock=$TEST_DIR/imuxB.sock.$test_hash
  imuxAstderr=$TEST_DIR/imuxA.stderr.$test_hash
  imuxBstderr=$TEST_DIR/imuxB.stderr.$test_hash
  outfile=$TEST_DIR/sockimux_outfile.$test_hash

  ./sockimux $imuxAsock > $outfile 2> $imuxAstderr &
  imuxApid=$!

  timeout $TIMEOUT  bash -c "cat $infile | ./sockimux $imuxBsock 2> $imuxBstderr" &
  imuxBpid=$!

  sleep 0.5

  socat_pids=$(connect_socks $imuxAsock $imuxBsock $num_streams)

  wait $imuxBpid
  timeout_code=$?

  sleep 0.5

  kill $socat_pids > /dev/null 2>&1
  kill $imuxApid > /dev/null 2>&1
  rm $imuxAsock $imuxBsock

  if [ $timeout_code -eq 124 ]
  then
    printf "{RED}[FAIL]${NC} Test timed out!\n"
    exit 1
  fi

  if [ ! $timeout_code -eq 0 ]
  then
    printf "${RED}[FAIL]${NC} ./sockimux exited with code ${timeout_code}\n"
    exit 1
  fi

  infile_checksum=$(md5sum $infile | cut -d' ' -f1)
  outfile_checksum=$(md5sum $outfile | cut -d' ' -f1)

  if [ "$infile_checksum" != "$outfile_checksum" ]
  then
    printf "${RED}[FAIL]${NC} Input file checksum does not match output file checksum.\n"
    echo $infile_checksum $outfile_checksum
    exit 1
  fi

  printf "${GREEN}[PASS]${NC} ${desc}\n"

}

run_test $INFILE 1 "Copy file across with one stream"
run_test $INFILE 24 "Copy file across with 24 streams"

