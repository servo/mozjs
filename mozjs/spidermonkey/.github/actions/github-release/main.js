const core = require('@actions/core');
const path = require("path");
const fs = require("fs");
const github = require('@actions/github');
const glob = require('glob');

function sleep(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds))
}

async function runOnce() {
  // Load all our inputs and env vars. Note that `getInput` reads from `INPUT_*`
  const files = core.getInput('files');
  const name = core.getInput('name');
  const token = core.getInput('token');
  const slug = process.env.GITHUB_REPOSITORY;
  const owner = slug.split('/')[0];
  const repo = slug.split('/')[1];
  const sha = process.env.GITHUB_SHA;

  core.info(`files: ${files}`);
  core.info(`name: ${name}`);
  core.info(`token: ${token}`);

  const octokit = github.getOctokit(token);

  // Try to load the release for this tag, and if it doesn't exist then make a
  // new one. We might race with other builders on creation, though, so if the
  // creation fails try again to get the release by the tag.
  let release = null;
  try {
    core.info(`fetching release`);
    release = await octokit.rest.repos.getReleaseByTag({ owner, repo, tag: name });
  } catch (e) {
    console.log("ERROR: ", JSON.stringify(e, null, 2));
    core.info(`creating a release`);
    try {
      release = await octokit.rest.repos.createRelease({
        owner,
        repo,
        tag_name: name,
        prerelease: name === 'dev',
      });
    } catch(e) {
      console.log("ERROR: ", JSON.stringify(e, null, 2));
      core.info(`fetching one more time`);
      release = await octokit.rest.repos.getReleaseByTag({ owner, repo, tag: name });
    }
  }
  console.log("found release: ", JSON.stringify(release.data, null, 2));

  // Upload all the relevant assets for this release as just general blobs.
  for (const file of glob.sync(files)) {
    const size = fs.statSync(file).size;
    const name = path.basename(file);
    for (const asset of release.data.assets) {
      if (asset.name !== name)
        continue;
      console.log(`deleting prior asset ${asset.id}`);
      await octokit.rest.repos.deleteReleaseAsset({
        owner,
        repo,
        asset_id: asset.id,
      });
    }
    core.info(`upload ${file}`);
    await octokit.rest.repos.uploadReleaseAsset({
      data: fs.createReadStream(file),
      headers: { 'content-length': size, 'content-type': 'application/octet-stream' },
      name,
      url: release.data.upload_url,
    });
  }
}

async function run() {
  const retries = 10;
  for (let i = 0; i < retries; i++) {
    try {
      await runOnce();
      break;
    } catch (e) {
      if (i === retries - 1)
        throw e;
      logError(e);
      console.log("RETRYING after 10s");
      await sleep(10000)
    }
  }
}

function logError(e) {
  console.log("ERROR: ", e.message);
  try {
    console.log(JSON.stringify(e, null, 2));
  } catch (e) {
    // ignore json errors for now
  }
  console.log(e.stack);
}

run().catch(err => {
  logError(err);
  core.setFailed(err.message);
});
