#!/bin/bash

function uploadToBitbucket() {

usr=$1; pwd=$2; pge=$3; fil=$4

curl --insecure               `# make tls cert validation optional, read this if you need it (https://curl.haxx.se/docs/sslcerts.html) ` \
       --progress-bar           `# print the progress visually                                                                          ` \
       --output ./utbb          `# avoid outputting anything apart from that neat bar                                                  ` \
       --location               `# follow redirects if we are told so                                                                 ` \
       --fail                   `# ensure that we are not succeeding when the server replies okay but with an error code             ` \
       --write-out %{http_code} `# write the returned error code to stdout, we will redirect it later to a file for parsing         ` \
       --user "$usr:$pwd"       `# basic auth so that it lets us in                                                                ` \
       --form files=@"$fil" "https://api.bitbucket.org/2.0/repositories/${pge#/}" 1> utbb_httpcode # <- that special #/ thing trims initial slashes, if any

# -> when curl proceeds okay but the server is not happy:
if [ -f ./utbb ] && [ ! -z "$(grep error ./utbb)" ]; then
    cat <<-EOF

    [!] server error: bitbucket (the platform) returned a message for us to see:

    $(cat ./utbb)

EOF

    # custom error code for server-side issues
    exit 255
fi

# -> when the server responds with an http code other than 201 (created):
if [ -f ./utbb_httpcode ] && [ ! -z "$(cat ./utbb_httpcode)" ] && [ "$(cat ./utbb_httpcode)" -ne 201 ]; then
    cat <<-EOF

    [!] server error: bitbucket (the platform) returned HTTP error code $(cat ./utbb_httpcode)

EOF

    # custom error code for server-side issues
    exit 254
fi

# -> when curl has general connectivity problems at network level:
if [ $? -ne 0 ]; then
    cat <<-EOF

    [!] error: curl returned exit code $?... upload canceled!
               to see what the number means go here:
               <https://curl.haxx.se/docs/manpage.html#EXIT>

EOF
fi
}

file_to_upload=$1
renamed_file="$(dirname $file_to_upload)/$BUILD_CUSTOM_NAME.$(basename $file_to_upload)"

cp $file_to_upload $renamed_file

uploadToBitbucket $USERNAME $PASSWORD /suwissagl/bldc/downloads $renamed_file

