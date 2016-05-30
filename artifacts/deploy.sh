#!/bin/bash
set -e

SOURCE_BRANCH="master"
TARGET_BRANCH="gh-pages"

function doCompile {
mkdir -p out/centos7
docker build --rm=true -t local/zabbix-docker-module-compilation -f dockerfiles/centos/Dockerfile .
docker run --rm -v $PWD/out/centos7:/tmp local/zabbix-docker-module-compilation cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so
docker rmi -f local/zabbix-docker-module-compilation
cd out/centos7
md5sum zabbix_module_docker.so > md5sum.txt
sha1sum zabbix_module_docker.so > sha1sum.txt
sha256sum zabbix_module_docker.so > sha256sum.txt
cd ../..

mkdir -p out/debian8
docker build --rm=true -t local/zabbix-docker-module-compilation -f dockerfiles/debian/Dockerfile .
docker run --rm -v $PWD/out/debian8:/tmp local/zabbix-docker-module-compilation cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so
docker rmi -f local/zabbix-docker-module-compilation
cd out/debian8
md5sum zabbix_module_docker.so > md5sum.txt
sha1sum zabbix_module_docker.so > sha1sum.txt
sha256sum zabbix_module_docker.so > sha256sum.txt
cd ../..

mkdir -p out/ubuntu14
docker build --rm=true -t local/zabbix-docker-module-compilation -f dockerfiles/ubuntu/Dockerfile .
docker run --rm -v $PWD/out/ubuntu14:/tmp local/zabbix-docker-module-compilation cp /root/zabbix/src/modules/zabbix_module_docker/zabbix_module_docker.so /tmp/zabbix_module_docker.so
docker rmi -f local/zabbix-docker-module-compilation
cd out/ubuntu14
md5sum zabbix_module_docker.so > md5sum.txt
sha1sum zabbix_module_docker.so > sha1sum.txt
sha256sum zabbix_module_docker.so > sha256sum.txt
cd ../..
}

# Pull requests and commits to other branches shouldn't try to deploy, just build to verify
if [ "$TRAVIS_PULL_REQUEST" != "false" -o "$TRAVIS_BRANCH" != "$SOURCE_BRANCH" ]; then
    echo "Skipping deploy; just doing a build."
    doCompile
    exit 0
fi

# Save some useful information
REPO=`git config remote.origin.url`
SSH_REPO=${REPO/https:\/\/github.com\//git@github.com:}
SHA=`git rev-parse --verify HEAD`

# Clone the existing gh-pages for this repo into out/
# Create a new empty branch if h-pages doesn't exist yet (should only happen on first deply)
git clone $REPO out
cd out
git checkout $TARGET_BRANCH || git checkout --orphan $TARGET_BRANCH
cd ..

# Clean out existing contents
rm -rf out/**/* || exit 0

# Run our compile script
doCompile

# Now let's go have some fun with the cloned repo
cd out
git config user.name "Travis CI"
git config user.email "$COMMIT_AUTHOR_EMAIL"

# If there are no changes to the compiled out (e.g. this is a README update) then just bail.
if [ -z `git diff --exit-code` ]; then
    echo "No changes to the output on this push; exiting."
    exit 0
fi

# Commit the "changes", i.e. the new version.
# The delta will show diffs between new and old versions.
git add centos7
git add debian8
git add ubuntu14
git commit -m "Deploy to GitHub Pages: ${SHA}"

# Get the deploy key by using Travis's stored variables to decrypt deploy_key.enc
ENCRYPTED_KEY_VAR="encrypted_${ENCRYPTION_LABEL}_key"
ENCRYPTED_IV_VAR="encrypted_${ENCRYPTION_LABEL}_iv"
ENCRYPTED_KEY=${!ENCRYPTED_KEY_VAR}
ENCRYPTED_IV=${!ENCRYPTED_IV_VAR}
openssl aes-256-cbc -K $ENCRYPTED_KEY -iv $ENCRYPTED_IV -in ../artifacts/deploy_key.enc -out deploy_key -d
chmod 600 deploy_key
eval `ssh-agent -s`
ssh-add deploy_key

# Now that we're all set up, we can push.
git push $SSH_REPO $TARGET_BRANCH

