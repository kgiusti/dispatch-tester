#!/usr/bin/env bash

#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

# Run a simple test over SSL

set -x

CONFIG=$(dirname $0)/config.null
TEST_CERT_DIR=`pwd`/test_cert_dir
CERT_DB=${TEST_CERT_DIR}/test_cert_db
CERT_PW_FILE=`pwd`/cert.password
TEST_HOSTNAME=127.0.0.1
TEST_CLIENT_CERT=rumplestiltskin
CA_PEM_FILE=${TEST_CERT_DIR}/ca_cert.pem
OTHER_CA_CERT_DB=${TEST_CERT_DIR}/x_ca_cert_db
OTHER_CA_PEM_FILE=${TEST_CERT_DIR}/other_ca_cert.pem
PY_PING_BROKER=/home/kgiusti/tmp/ping_broker
COUNT=10

trap cleanup EXIT

if [[ !(-e ${CERT_PW_FILE}) ]] ;  then
    echo password > ${CERT_PW_FILE}
fi

CERTUTIL=$(type -p certutil)
if [[ !(-x $CERTUTIL) ]] ; then
    echo "No certutil, failed!";
    exit 0;
fi
PK12UTIL=$(type -p pk12util)
if [[ !(-x $PK12UTIL) ]] ; then
    echo >&2 "'pk12util' command not available, failed!"
    exit 0
fi
OPENSSL=$(type -p openssl)
if [[ !(-x $OPENSSL) ]] ; then
    echo >&2 "'openssl' command not available, failed!"
    exit 0
fi
KEYTOOL=$(type -p keytool)
if [[ !(-x $KEYTOOL) ]] ; then
    echo >&2 "'keytooll' command not available, failed!"
    exit 0
fi


error() { echo $*; exit 1; }

create_certs() {

    local CERT_SUBJECT=${1:-"CN=${TEST_HOSTNAME},O=MyCo,ST=Massachusetts,C=US"}
    local CERT_SAN=${2:-"*.server.com"}

    mkdir -p ${TEST_CERT_DIR}
    rm -rf ${TEST_CERT_DIR}/*

    # Set Up a CA with a self-signed Certificate.  This database will be loaded by the broker.
    #
    mkdir -p ${CERT_DB}
    certutil -N -d ${CERT_DB} -f ${CERT_PW_FILE}
    certutil -S -d ${CERT_DB} -n "Test-CA" -s "CN=Test-CA,O=MyCo,ST=Massachusetts,C=US" -t "CT,," -x -f ${CERT_PW_FILE} -z /bin/sh >/dev/null 2>&1
    certutil -L -d ${CERT_DB} -n "Test-CA" -a -o ${CERT_DB}/rootca.crt -f ${CERT_PW_FILE}

    # create broker certificate signed by Test-CA
    #
    certutil -R -d ${CERT_DB} -s "${CERT_SUBJECT}" -o ${TEST_CERT_DIR}/broker.req -f ${CERT_PW_FILE} -z /bin/sh > /dev/null 2>&1
    certutil -C -d ${CERT_DB} -c "Test-CA" -8 "${CERT_SAN}" -i ${TEST_CERT_DIR}/broker.req -o ${TEST_CERT_DIR}/broker.crt -f ${CERT_PW_FILE} -m ${RANDOM}
    certutil -A -d ${CERT_DB} -n ${TEST_HOSTNAME} -i ${TEST_CERT_DIR}/broker.crt -t "Pu,,"
    rm ${TEST_CERT_DIR}/broker.req
    rm ${TEST_CERT_DIR}/broker.crt


    # create a certificate to identify broker clients
    #
    certutil -R -d ${CERT_DB} -s "CN=${TEST_CLIENT_CERT}" -o ${TEST_CERT_DIR}/client.req -f ${CERT_PW_FILE} -z /bin/sh > /dev/null 2>&1
    certutil -C -d ${CERT_DB} -c "Test-CA" -8 "*.client.com" -i ${TEST_CERT_DIR}/client.req -o ${TEST_CERT_DIR}/client.crt -f ${CERT_PW_FILE} -m ${RANDOM}
    certutil -A -d ${CERT_DB} -n ${TEST_CLIENT_CERT} -i ${TEST_CERT_DIR}/client.crt -t "Pu,,"
    rm ${TEST_CERT_DIR}/client.req
    rm ${TEST_CERT_DIR}/client.crt


    # extract the CA's certificate
    $PK12UTIL -o ${TEST_CERT_DIR}/CA_pk12.out -d ${CERT_DB} -n "Test-CA"  -w ${CERT_PW_FILE} -k ${CERT_PW_FILE} > /dev/null
    $OPENSSL pkcs12 -in ${TEST_CERT_DIR}/CA_pk12.out -out ${CA_PEM_FILE} -nokeys -passin file:${CERT_PW_FILE} >/dev/null


    # Create a certificate for the server end of an inter-router connection.  Use the CA's certificate to sign it:
    keytool -storetype pkcs12 -keystore ${TEST_CERT_DIR}/router-server.pkcs12 -storepass password -alias router-server-certificate -keypass password -genkey  -dname "CN=127.0.0.1" -ext san=dns:localhost,dns:localhost.localdomain -validity 99999
    keytool -storetype pkcs12 -keystore ${TEST_CERT_DIR}/router-server.pkcs12 -storepass password -alias router-server-certificate -keypass password -certreq -file router-server-request.pem
    keytool -storetype pkcs12 -keystore ${TEST_CERT_DIR}/CA_pk12.out -storepass password -alias "Test-CA" -keypass password -gencert -rfc -validity 99999 -infile router-server-request.pem -outfile ${TEST_CERT_DIR}/router-server-certificate.pem
    openssl pkcs12 -nocerts -passin pass:password -in ${TEST_CERT_DIR}/router-server.pkcs12 -passout pass:password -out ${TEST_CERT_DIR}/router-server-private-key.pem
    rm router-server-request.pem

    # Create a certificate for clients of the router (including the connecting end of a router connection).  Use the CA's certificate to sign it:
    keytool -storetype pkcs12 -keystore ${TEST_CERT_DIR}/router-client.pkcs12 -storepass password -alias router-client-certificate -keypass password -genkey  -dname "O=Client,CN=${TEST_CLIENT_CERT}" -validity 99999
    keytool -storetype pkcs12 -keystore ${TEST_CERT_DIR}/router-client.pkcs12 -storepass password -alias router-client-certificate -keypass password -certreq -file router-client-request.pem
    keytool -storetype pkcs12 -keystore ${TEST_CERT_DIR}/CA_pk12.out -storepass password -alias "Test-CA" -keypass password -gencert -rfc -validity 99999 -infile router-client-request.pem -outfile ${TEST_CERT_DIR}/router-client-certificate.pem
    openssl pkcs12 -nocerts -passin pass:password -in ${TEST_CERT_DIR}/router-client.pkcs12 -passout pass:password -out ${TEST_CERT_DIR}/router-client-private-key.pem
    rm router-client-request.pem

}

delete_certs() {
    if [[ -e ${TEST_CERT_DIR} ]] ;  then
        rm -rf ${TEST_CERT_DIR}
    fi
}

# Don't need --no-module-dir or --no-data-dir as they are set as env vars in test_env.sh
COMMON_OPTS="--daemon --config $CONFIG --ssl-cert-db $CERT_DB --ssl-cert-password-file $CERT_PW_FILE --ssl-cert-name $TEST_HOSTNAME"

# Start new brokers:
#   $1 must be integer
#   $2 = extra opts
# Append used ports to PORTS variable
# start_brokers() {
#     local -a ports
#     for (( i=0; $i<$1; i++)) do
#     ports[$i]=$(qpidd --port 0 --interface 127.0.0.1 $COMMON_OPTS $2) || error "Could not start broker $i"
#     done
#     PORTS=( ${PORTS[@]} ${ports[@]} )
# }

# # Stop single broker:
# #   $1 is number of broker to stop (0 based)
# stop_broker() {
#     qpidd -qp ${PORTS[$1]}

#     # Remove from ports array
#     unset PORTS[$1]
# }

# stop_brokers() {
#     for port in "${PORTS[@]}";
#     do
#         qpidd -qp $port
#     done
#     PORTS=()
# }

# pick_port() {
#     # We need a fixed port to set --cluster-url. Use qpidd to pick a free port.
#     PICK=`qpidd --listen-disable ssl -dp0`
#     qpidd -qp $PICK
#     echo $PICK
# }

cleanup() {
    echo "CLEANED"
    #stop_brokers
    #delete_certs
    #rm -f ${CERT_PW_FILE}
}


delete_certs
create_certs || error "Could not create test certificate database"

# start_ssl_broker
# PORT=${PORTS[0]}
# echo "Running SSL test on port $PORT"
# export QPID_NO_MODULE_DIR=1
#export QPID_SSL_CERT_DB=${CERT_DB}
#export QPID_SSL_CERT_PASSWORD_FILE=${CERT_PW_FILE}


# ## Test using the Python client
# echo "Testing Non-Authenticating with Python Client..."
# URL=amqps://$TEST_HOSTNAME:$PORT
# if `$PY_PING_BROKER -b $URL`; then echo "    Passed"; else { echo "    Failed"; exit 1; }; fi

# #### Client Authentication tests


# stop_brokers

# ### Additional tests that require 'openssl' and 'pk12util' to be installed (optional)

# PK12UTIL=$(type -p pk12util)
# if [[ !(-x $PK12UTIL) ]] ; then
#     echo >&2 "'pk12util' command not available, skipping remaining tests"
#     exit 0
# fi

# OPENSSL=$(type -p openssl)
# if [[ !(-x $OPENSSL) ]] ; then
#     echo >&2 "'openssl' command not available, skipping remaining tests"
#     exit 0
# fi

# if test -d $PYTHON_DIR; then
# ## verify python version > 2.5 (only 2.6+ does certificate checking)
#     PY_VERSION=$(python -c "import sys; print hex(sys.hexversion)")
#     if (( PY_VERSION < 0x02060000 )); then
# 	echo >&2 "Detected python version < 2.6 - skipping certificate verification tests"
# 	exit 0
#     fi

#     echo "Testing Certificate validation and Authentication with the Python Client..."

# extract the CA's certificate as a PEM file
#     get_ca_certs || error "Could not extract CA certificates as PEM files"
#     start_ssl_broker
#     PORT=${PORTS[0]}
#     URL=amqps://$TEST_HOSTNAME:$PORT
# # verify the python client can authenticate the broker using the CA
#     if `${PY_PING_BROKER} -b $URL --ssl-trustfile=${CA_PEM_FILE}`; then echo "    Passed"; else { echo "    Failed"; exit 1; }; fi
# # verify the python client fails to authenticate the broker when using the other CA
#     if `${PY_PING_BROKER} -b $URL --ssl-trustfile=${OTHER_CA_PEM_FILE} > /dev/null 2>&1`; then { echo "    Failed"; exit 1; }; else echo "    Passed"; fi
#     stop_brokers

# # create a certificate without matching TEST_HOSTNAME, should fail to verify

#     create_certs "O=MyCo" "*.${TEST_HOSTNAME}.com" || error "Could not create server test certificate"
#     get_ca_certs || error "Could not extract CA certificates as PEM files"
#     start_ssl_broker
#     PORT=${PORTS[0]}
#     URL=amqps://$TEST_HOSTNAME:$PORT
#     if `${PY_PING_BROKER} -b $URL --ssl-trustfile=${CA_PEM_FILE} > /dev/null 2>&1`; then { echo "    Failed"; exit 1; }; else echo "    Passed"; fi
# # but disabling the check for the hostname should pass
#     if `${PY_PING_BROKER} -b $URL --ssl-trustfile=${CA_PEM_FILE} --ssl-skip-hostname-check`; then echo "    Passed"; else { echo "    Failed"; exit 1; }; fi
#     stop_brokers

# # test SubjectAltName parsing

#     if (( PY_VERSION >= 0x02070300 )); then
#     # python 2.7.3+ supports SubjectAltName extraction
#     # create a certificate with TEST_HOSTNAME only in SAN, should verify OK
# 	create_certs "O=MyCo" "*.foo.com,${TEST_HOSTNAME},*xyz.com" || error "Could not create server test certificate"
# 	get_ca_certs || error "Could not extract CA certificates as PEM files"
# 	start_ssl_broker
# 	PORT=${PORTS[0]}
# 	URL=amqps://$TEST_HOSTNAME:$PORT
# 	if `${PY_PING_BROKER} -b $URL --ssl-trustfile=${CA_PEM_FILE}`; then echo "    Passed"; else { echo "    Failed"; exit 1; }; fi
# 	stop_brokers

# 	create_certs "O=MyCo" "*${TEST_HOSTNAME}" || error "Could not create server test certificate"
# 	get_ca_certs || error "Could not extract CA certificates as PEM files"
# 	start_ssl_broker
# 	PORT=${PORTS[0]}
# 	URL=amqps://$TEST_HOSTNAME:$PORT
# 	if `${PY_PING_BROKER} -b $URL --ssl-trustfile=${CA_PEM_FILE}`; then echo "    Passed"; else { echo "    Failed"; exit 1; }; fi
# 	stop_brokers
#     fi

# fi

