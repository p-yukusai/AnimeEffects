# Export destination research, batch 2

Checked 2026-09-11. One row per (platform, what you are posting). Primary sources only; every number below is quoted from a platform-owned page, and anything that could not be confirmed from one is `null` and named in **Gaps**.

Machine-readable rows: `tools/export_targets_research_batch2.jsonl` (18 rows, exactly the 14 agreed keys).

Rows: 18. Confidence: **verified 10**, **verified-partial 4**, **verified-conflicting 4**, unverified 0.

## Rows

| # | Platform (what you are posting) | Byte cap | Duration cap | Dimensions and codecs | Re-enc | Alpha | Loop | Confidence |
|---|---|---|---|---|---|---|---|---|
| 1 | Nico Nico Douga (video upload) | 6 GB (hard) | 6 h | up to 4096x2160 horizontal / 2160x4096 vertical, 60 fps fixed, mp4 H.264 recommended | yes | no | no | verified |
| 2 | Nico Nico Seiga (illustration post) | 10 MB (hard) | none | png, jpeg, gif; no pixel cap published | unknown | unknown | unknown | verified-partial |
| 3 | Douyin (video post) | 4 GB (own docs also say 128 MB) | 3600 s (own docs also say 900 s) | short side < 2160, long side < 4096; mp4/webm | yes | no | no | verified-conflicting |
| 4 | Facebook (feed video post) | none published | none published | max width 4000 px, divisible by 16, 30 fps or lower, MP4/MOV H.264 + AAC | yes | no | no | verified-partial |
| 5 | Facebook Reels (video post) | none published | 90 s via API, any length in the app | 9:16, 1080x1920 recommended, 540x960 minimum, 24 to 60 fps, H.264/H.265/VP9/AV1 | yes | no | no | verified-conflicting |
| 6 | Facebook (photo post) | 10 MB API hard, 15 MB recommended | none | JPEG/BMP/PNG/GIF/TIFF; PNG above 1 MB warned to look pixelated | yes | unknown | unknown | verified-conflicting |
| 7 | Tumblr (video post) | 500 MB (hard) | 600 s | MOV or MP4; transcoded toward 720p H.264 MP4 | yes | no | no | verified |
| 8 | Tumblr (photo post) | 20 MB static, about 5 MB for GIFs | none | 540x810 recommended, large view 2048x3072; GIF converted to GIFV | yes | unknown | yes | verified |
| 9 | Pinterest (video Pin) | none published | 300 s | MP4/M4V web, MP4/MOV/M4V mobile, H.264 or H.265, 9:16 recommended | unknown | no | no | verified |
| 10 | Pinterest (image Pin) | 20 MB web, 1 GB recommended on mobile | none | BMP/JPEG/PNG/TIFF/WEBP, GIF not listed | yes | unknown | no | verified |
| 11 | Snapchat (Spotlight video) | none published | minimum 5 s, no maximum published | vertical video with sound; no format or pixel spec published | unknown | no | unknown | verified-partial |
| 12 | Dailymotion (video upload) | 4 GB (64 GB on the top tier) | 7200 s | up to 3840x2160 / 2160x3840, 24/25/30/50/60 fps, MOV/MP4/AVI/WMV/MKV/FLV | yes | no | no | verified |
| 13 | Vimeo (video upload) | keep under 200 GB (soft) | 86400 s (hard refusal) | MP4/MOV preferred, H.264/ProRes 422 HQ/H.265, square pixels, constant frame rate | yes | no | no | verified |
| 14 | DeviantArt (image or animated GIF submission) | 80 MB (second official page says 95 MB) | none | JPEG, PNG, GIF; no pixel cap published | unknown | unknown | unknown | verified-conflicting |
| 15 | DeviantArt (video or film submission, Core Members) | 1 GB | 720 s on Core and Core+, none on Core Pro/Pro+ | recommended 1920x1080, first frame becomes the preview | unknown | no | unknown | verified |
| 16 | Newgrounds (animation or movie upload) | none published | none published | SWF and most video formats; 3840x2160, 1920x1080, 1280x720, 640x380; suggested 10 to 20 Mbps at 1080p | no-partial | unknown | yes | verified-partial |
| 17 | pixiv (illustration post) | 32 MB per image, 200 MB per post | none | JPG, PNG, GIF; no pixel cap published; mixed-format posts all convert to JPG | yes | unknown | no | verified |
| 18 | Instagram (feed photo post) | 8 MB (hard) | none | JPEG only, width 320 to 1440 px, ratio 4:5 to 1.91:1, sRGB | yes | no | no | verified |

## Conflicts carried, not resolved

These are cases where two platform-owned sources disagree. Both values are quoted in the JSONL note field; the conservative value is the one in the numeric field.

- **Douyin**: the upload endpoint says 4 GB total and chunking mandatory above 300 MB; the chunked-upload init page (dated 2024-04-09) says 128 MB and 15 minutes and chunking mandatory above 128 MB. Duration additionally conflicts with the Android share SDK, which says 60 minutes per single video.
- **Facebook Reels**: Meta Business Help says reels can be any length; the Reels Publishing API enforces 3 to 90 seconds and rejects others with error 1363128. Recorded 90 s because it is the enforced API number.
- **Facebook photo post**: the Graph API says a photo must be less than 10 MB and that animated photos are not supported; the help centre recommends below 15 MB and lists GIF as a recommended photo format.
- **DeviantArt**: one help article says images are 80 MB, another says 95 MB. Recorded 80 MB so a file that passes our check passes both.
- **Vimeo**: the "keep files under 200 GB" advice, the Starter plan's 250 GB per video for legacy subscribers, and the uploader page's "there are no size limits for the video file" cannot all be true. Recorded 200 GB as a soft ceiling.

## Advertising specs that must not be used as organic caps

The single biggest risk in this batch. Each of these figures is a real, quotable platform number that applies to paid creatives, not to what our users are posting.

- **Facebook video, 4 GB and 240 minutes**: from the Facebook Reels ads guide and the Video Requirements ads one-sheeter. No organic byte or duration cap is published.
- **Pinterest video Pin, 2 GB and 15 minutes**: from the Review ad specs page. The organic Pin spec says 4 seconds to 5 minutes and publishes no byte cap. Guides and scheduling tools misattribute the ad numbers constantly.
- **Snapchat Spotlight, 1 GB, 3 to 30 minutes and 5 MB images**: from the Ads Manager specification (businesshelp.snapchat.com). Organic Spotlight publishes a 5 second minimum and nothing else.
- **Douyin, 500 MB**: repeated by Chinese SEO articles citing "创作者服务协议 4.2" and a 2024 help centre note. No ByteDance page containing that clause could be fetched, and the publicly reachable Douyin user agreement does not render without JavaScript. Unverified, so not recorded.

## Other traps found

- **Nico Nico Douga's vertical Short surface is 180 seconds**, against 6 hours for a normal upload on the same page. Same platform, 120x difference.
- **Nico Nico Douga is 6 GB, not 3 GB**, since 2024-10-29. Older guides are one revision behind, and some are two.
- **A Facebook feed video destination no longer exists as a separate surface.** Since the September 2025 change every video posted to Facebook is shared as a reel, and Meta says the same export settings apply to all video.
- **Facebook's maximum width of 4000 px with dimensions divisible by 16** is an export recommendation that an animation tool can satisfy exactly, and is worth encoding in the preset.
- **Vimeo has no alpha support at all** ("Vimeo does not include alpha channel support at this time"), so a transparent cutout export must never be aimed at it.
- **Tumblr's animated GIF ceiling is about 5 MB**, not the 20 MB static image cap, and a GIF above 5 MB that cannot be served is reduced to its first frame.
- **DeviantArt's animated GIF counts as an image and gets the 80 MB image cap**, which is 16x Tumblr's effective GIF ceiling. Pixiv, by contrast, rejects animated GIF and APNG outright and sends animation to its ugoira format.
- **pixiv is 32 MB per image and 200 MB per post**, not the 8 MB that blogs still quote (that was the pre-2018 limit); one 2026 article claims 10 MB, which matches no pixiv source.
- **Instagram feed photos are JPEG only**, so transparency cannot survive there regardless of byte budget.
- **Newgrounds treats loop as an upload option for MP4 files**, which is unusually useful for a looping animation export.
- **Meta's /event/videos Graph API edge documented 1 GB / 20 minutes and 1.75 GB / 45 minutes**, but the edge now reads "This edge has been deprecated and can no longer be used", so those numbers are dead.

## Gaps

Platforms and fields where no primary source exists. Values are `null` in the JSONL; nothing was guessed and nothing was filled with a plausible-looking number.

**Byte cap missing entirely, no primary source at all:**

- **Facebook feed video post**: Meta publishes no organic byte cap. Only the advertising 4 GB exists, which is a different surface.
- **Facebook Reels**: no organic byte cap. The Reels Publishing API documents a duration, frame rate, resolution and aspect ratio but no file size.
- **Snapchat Spotlight**: no byte cap, no maximum duration, no accepted-format list and no pixel specification for organic submissions. The help centre documents only the 5 second minimum and the vertical requirement; the Spotlight Guidelines are a conduct document.
- **Newgrounds**: no published byte cap anywhere. The wiki says only that a limit exists on the upload form and that it can be raised on request, so the limit is per-account and visible only after signing in. The commonly repeated 100 MB (SWF) and 6.3 GB (video) figures come from a member forum post, not from Newgrounds documentation.
- **Pinterest video Pin**: no byte cap. The organic spec publishes format, encoding and duration only.
- **Douyin consumer app and web upload**: ByteDance publishes limits only for its Open Platform and SDK paths. The 500 MB app figure is unverified.

**Field-level gaps on rows that otherwise have data:**

- **Nico Nico Seiga**: no pixel limit, and no statement about whether a posted illustration is re-encoded, whether PNG alpha survives, or whether an animated GIF animates.
- **pixiv**: no published pixel limit (a third-party 32768 px claim could not be confirmed), and no statement on alpha preservation.
- **DeviantArt**: no pixel limit on either row, no publication date on either help article, and no statement on re-encoding or alpha.
- **Instagram feed photo**: the 8 MB and width figures are the professional-account API specification; Instagram's in-app uploader is not documented to this level anywhere Meta publishes.
- **Facebook photo post**: no pixel cap, and no statement on whether transparency survives recompression.
- **Tumblr**: no statement on alpha, and neither help article carries an update date in its body (page metadata reads 2016), so the 500 MB and 20 MB figures are current but not recently reaffirmed.
- **Vimeo**: no pixel cap published.
- **Nico Nico Douga**: no alpha and no loop behaviour, which are absences rather than surprises (H.264 MP4 cannot carry alpha).

## Freshness notes

Fresh primary sources, 2025 or 2026: Nico Nico help pages (published 2026-04-15, one updated 2026-07-14), Nico Nico Seiga help (updated 2025-12-24), Dailymotion upload policy (updated within hours of the read), Instagram IG User Media reference (updated 2026-08-12), Meta Business Help Reels requirements (post-June-2025 text), Facebook video export settings (September 2025 update), Meta ads guide, Snapchat help and creator help.

Older primary sources, used and flagged as such in the notes: Tumblr help articles (page metadata 2016, corroborated by a 2021 staff announcement), DeviantArt help articles (no dates shown), Newgrounds wiki (no dates shown), pixiv limits (2018 announcement, restated on current help pages), Douyin chunked-upload page (updated 2024-04-09).
