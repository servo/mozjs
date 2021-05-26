#!/bin/bash

for FILE in etc/patches/*.patch; do
clear
echo $FILE
git apply --reject --directory=mozjs $FILE || break
done
