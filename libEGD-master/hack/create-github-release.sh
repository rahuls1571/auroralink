#!/bin/bash

if [ $# -lt 4 ]; then
  echo "Must provide version, text body, branch and token as inputs"
  exit 1
fi

version=$1
text=$2
branch=$3
token=$4
repo_name=$(git config --get remote.origin.url | sed -e 's%.*/%%' -e 's%.git$%%')
repo_name_lower="$(echo ${repo_name} | tr 'A-Z' 'a-z')"

generate_post_data()
{
  cat <<EOF
{
  "tag_name": "$version",
  "target_commitish": "$branch",
  "name": "$version",
  "body": "$text",
  "draft": false,
  "prerelease": true
}
EOF
}

echo "Delete old version of the release if available"
OLD_REL=$(curl "https://github.build.ge.com/api/v3/repos/DigitalGhost/$repo_name/releases/tags/${version}?access_token=$token" | grep -oP '"id": .*?,' | head -1 | cut -d' ' -f2 | sed 's/,//')

if ! [ -z "${OLD_REL}" ]; then
  echo "Deleting ${OLD_REL}"
  curl -X DELETE "https://github.build.ge.com/api/v3/repos/DigitalGhost/$repo_name/releases/${OLD_REL}?access_token=$token"
fi

echo "Create release $version for repo: $repo_name branch: $branch"
curl --data "$(generate_post_data)" "https://github.build.ge.com/api/v3/repos/DigitalGhost/$repo_name/releases?access_token=$token"


NEW_REL=$(curl "https://github.build.ge.com/api/v3/repos/DigitalGhost/$repo_name/releases/tags/${version}?access_token=$token" | grep -oP '"id": .*?,' | head -1 | cut -d' ' -f2 | sed 's/,//')

# Upload all deb packages
deb_files="$(find $(pwd) -name '*.deb')"

echo "Uploading assets to ${NEW_REL}"
FILES="\
    ${deb_files} \
      "
for FILE in ${FILES}; do
  echo "Uploading $FILE"
  curl -H "Authorization: token $token" \
       -H "Content-Type: $(file -b --mime-type ${FILE})" \
       --data-binary @$FILE \
       -X POST \
       "https://github.build.ge.com/api/uploads/repos/DigitalGhost/$repo_name/releases/${NEW_REL}/assets?name=$(basename ${FILE})"
done
