#!/bin/bash

# Script d'installation pour Scanner

if ! command -v sudo &> /dev/null; then
    echo "sudo est requis pour l'installation"
    exit 1
fi

echo "Installation des dépendances..."
if command -v apt &> /dev/null; then
    sudo apt update
    sudo apt-get install -y build-essential libcurl4-openssl-dev
elif command -v yum &> /dev/null; then
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y libcurl-devel
fi

echo "Compilation et installation du Scanner..."
make clean
make
make install

echo "Installation terminée !"
echo "Vous pouvez maintenant utiliser Scanner directement avec la commande : pfinders"