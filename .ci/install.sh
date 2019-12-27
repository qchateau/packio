#!/bin/bash

set -e
set -x

if [[ "$(uname -s)" == 'Darwin' ]]; then
    brew update || brew update
    brew outdated pyenv || brew upgrade pyenv
    brew install pyenv-virtualenv
    brew install cmake || true

    if which pyenv > /dev/null; then
        eval "$(pyenv init -)"
    fi

    pyenv install 3.6.7
    pyenv virtualenv 3.6.7 conan
    pyenv rehash
    pyenv activate conan
fi

pip3 install conan conan_package_tools --upgrade

conan user
