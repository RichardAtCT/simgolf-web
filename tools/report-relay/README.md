# Report relay

Lets players file GitHub issues from the in-game form without a GitHub
account. Until it's deployed, the form opens GitHub's new-issue page with the
report filled in instead.

1. Create a fine-grained GitHub token: repository `RichardAtCT/simgolf-web`
   only, permission **Issues: Read and write**.
2. `npx wrangler deploy` in this folder (free Cloudflare account), then
   `npx wrangler secret put GITHUB_TOKEN` and paste the token.
3. Set `REPORT_ENDPOINT` in `web/pages/report.js` to the worker's URL plus
   `/report` and publish the site.
