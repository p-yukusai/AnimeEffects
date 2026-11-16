# Platform upload limits for export presets — verified 2026-09-11

Research pass over 20 destinations x (A) video/image attachment and (B) sticker/emoji.
Every `verified` row was read from a primary source (official help centre, developer/API
reference, official upload guideline, or official client/lexicon source) on **2026-09-11**.
Machine-readable version with full per-row notes: `tools/export_targets_research.jsonl`.

Legend: **re-encodes** = server-side transcode/normalisation.
`yes-partial` = one sending path transcodes and another does not.
**Verified?** `verified` | `verified-partial` (some fields primary, some not) |
`verified-conflicting` (two official sources disagree — both recorded) | `unverified` (no primary source).

| Platform | Category | Container / Codec | Max size | Max duration | Dimensions | Re-encodes? | Alpha? | Loops? | Verified? | Source URL | Checked |
|---|---|---|---|---|---|---|---|---|---|---|---|
| WhatsApp | A. video attach | 3GP, AVI, FLV, MKV, MOV, MP4 (accepted containers); server transcode to H.264 | 64 MB (slow connection) / 100 MB (faster connection) — downscaled, not rejected; 2 GB only via Document | none for chat video (Status video 90 s) | No exact size. Resolution is capped by the transcode target: 720p on … | yes | no | no | verified | https://faq.whatsapp.com/453914586839706 | 2026-09-11 |
| WhatsApp | A. image attach | JPEG, PNG (accepted); server re-encode to JPEG | none published (media is compressed); 2 GB only via Document | none published | No exact requirement. faq recommends at least 192px height/width for … | yes | no | no | verified | https://faq.whatsapp.com/453914586839706 | 2026-09-11 |
| WhatsApp | B. sticker | animated: WebP ONLY (animated stickers are WebP only). Static: PNG or WebP. | 500 KB | 10 s | EXACTLY 512x512 px per sticker. Recommend a 16px margin between art a… | no | yes | yes | verified | https://github.com/WhatsApp/stickers/blob/main/Android/README.md | 2026-09-11 |
| Telegram | A. video attach | MP4 (H.264); Telegram clients support MPEG4 videos, other formats may be sent a… | 2 GB | none published | No documented limit for regular video. Telegram optimises each upload… | yes-partial | no | no | verified | https://telegram.org/faq | 2026-09-11 |
| Telegram | A. image attach | JPEG (photo path, auto-converted); PNG/WebP/GIF and any other format only if se… | none on the app photo path (recompressed); Bot API 10 MB multipart / 5 MB by URL; 2 GB (4 GB Premium) as a Document | none published | Photo geometry is checked separately from bytes; Bot API sendPhoto re… | yes | no | no | verified | https://core.telegram.org/bots/api | 2026-09-11 |
| Telegram | B. video sticker | WebM, VP9 codec. No audio stream. | 256 KB | 3 s | For stickers, one side must be EXACTLY 512 px; the other side may be … | no | yes | yes | verified | https://core.telegram.org/stickers | 2026-09-11 |
| Telegram | B. vector sticker | TGS (gzipped Lottie JSON, exported via the Bodymovin-TG After Effects plugin) | 64 KB | 3 s | Canvas EXACTLY 512x512. Objects must not leave the canvas. | no | yes | yes | verified | https://core.telegram.org/stickers | 2026-09-11 |
| Telegram | B. static sticker | PNG or WebP | not separately documented beyond the geometry rules | none published | One side EXACTLY 512 px, other side <= 512 px. Emoji: exactly 100x100. | no | yes | no | verified | https://core.telegram.org/stickers | 2026-09-11 |
| Telegram | B. sticker import | PNG, WebP or WebM accepted by the @Stickers bot | not separately documented beyond the geometry rules | none published | Presumably the same geometry rules as native creation; not restated o… | unclear | unknown | unknown | verified-partial | https://core.telegram.org/stickers | 2026-09-11 |
| Discord | A. file attach | No fixed codec list. Documented common types include JPEG, PNG, PDF, MP3, MOV, … | 20 MB | none published | No documented limit; media is inspected, converted and resized server… | yes | yes | yes | verified | https://support.discord.com/hc/en-us/articles/25444343291031-File-Attachments-Upload-Limits-FAQ | 2026-09-11 |
| Discord | B. sticker | PNG (static), APNG (animated), GIF (animated), Lottie JSON. API format_type enu… | 512 KB | 5 s | EXACTLY 320x320 px. Renders at 160x160 dp in the desktop chat, which … | yes | yes | yes | verified | https://support.discord.com/hc/en-us/articles/4402687377815-Tips-for-Sticker-Creators-FAQ | 2026-09-11 |
| Discord | B. emoji | Upload accepts JPEG, PNG, GIF, WebP, AVIF. All emoji are SERVED as WebP (client… | 256 KB | none published | 128x128 recommended; the API field is documented as 'the 128x128 emoj… | yes | yes | yes | verified | https://docs.discord.com/developers/resources/emoji | 2026-09-11 |
| Signal | A. video attach | MP4/H.264 on the wire after client-side transcode. Original container is preser… | 100 MB | none published | No documented limit. Videos are re-encoded to 720p at ~2 Mbps by defa… | yes | no | no | verified | https://raw.githubusercontent.com/signalapp/Signal-Android/main/app/src/main/java/org/thoughtcrime/securesms/util/RemoteConfig.kt | 2026-09-11 |
| Signal | A. image attach | JPEG output after client recompression; original PNG preserved only if sent as … | 100 MB | none published | Image dimension ladder by quality level: LEVEL_1 1600/1024/768/512; L… | yes | no | no | verified | https://raw.githubusercontent.com/signalapp/Signal-Android/main/app/src/main/java/org/thoughtcrime/securesms/mms/PushMediaConstraints.java | 2026-09-11 |
| Signal | B. sticker | Animated: APNG only ('Please do not upload GIFs'). Static: PNG or WebP. Cover: … | 300 KB | 3 s | Stickers resize to 512x512 px. Recommend a 16px margin around each st… | no | yes | yes | verified | https://support.signal.org/hc/en-us/articles/360031836512-Stickers | 2026-09-11 |
| Pixiv | B. ugoira (animated) | Two routes. (1) Multiple still frames: PNG or JPEG, all frames must share one f… | 30 MB total and 8 MB per frame (frame route); 16 MB (animated-GIF route) | none published | No fixed pixel size, but all frames must be identical in size. pixiv … | yes | unknown | yes | verified | https://www.pixiv.help/hc/en-us/articles/235584628-What-are-Ugoira | 2026-09-11 |
| Bluesky | A. video attach | The embed lexicon accepts ONLY video/mp4. Bluesky's own product blog states the… | 300 MB (300,000,000 bytes) — raised from 100 MB in Aug 2026; hard server-side rejection | 10 min (600 s) — raised from 3 min in Aug 2026 | No pixel-size limit. Aspect ratio must be supplied explicitly as meta… | yes | no | no | verified | https://raw.githubusercontent.com/bluesky-social/social-app/main/src/lib/constants.ts | 2026-09-11 |
| Mastodon | A. video attach | Video: MP4, M4V, MOV, WebM (protocol-level supported_mime_types also lists vide… | 99 MB default, INSTANCE-CONFIGURABLE (image 16 MB) | none published | Video matrix limit 8,294,400 px (~8.3 MP, i.e. 3840x2160); frame rate… | yes | no | yes | verified | https://docs.joinmastodon.org/user/posting/ | 2026-09-11 |
| Mastodon | B. emoji | Images whose MIME type is in the instance's IMAGE_MIME_TYPES - in practice PNG … | 256 KB | none published | No documented source dimension; the server generates resized variants… | yes | yes | yes | verified | https://raw.githubusercontent.com/mastodon/mastodon/main/app/models/custom_emoji.rb | 2026-09-11 |
| X | A. video (Post) | MP4 or MOV. Video codec H.264 High Profile. Audio AAC-LC (High-Efficiency AAC N… | 8 GB | 20 min | Dimensions must be between 32x32 and 1280x1024. Aspect ratio must be … | yes | no | no | verified | https://docs.x.com/x-api/media/quickstart/best-practices | 2026-09-11 |
| TikTok | A. video attach | MP4 (recommended), WebM, MOV. Codecs: H.264 (recommended), H.265, VP8, VP9. GIF… | 4 GB | 10 min | Minimum 360 px and maximum 4096 px for BOTH width and height. Frame r… | yes | no | no | verified | https://developers.tiktok.com/doc/content-posting-api-media-transfer-guide | 2026-09-11 |
| TikTok | A. image attach | WebP or JPEG. GIF not accepted. | 20 MB | none published | Maximum 1080p. | yes | no | no | verified | https://developers.tiktok.com/doc/content-posting-api-reference-photo-post/ | 2026-09-11 |
| YouTube | A. video attach | Widely accepting: MOV, MPEG-1/2/4, MP4, MPG, AVI, WMV, MPEGPS, FLV, 3GPP, WebM,… | 256 GB | 12 h | Recommended up to 4320p (7680x4320). Standard 16:9; the player adapts… | yes | no | no | verified | https://support.google.com/youtube/answer/71673 | 2026-09-11 |
| VK | A. video attach | AVI, MP4, 3GP, MPEG, MOV, FLV, F4V, WMV, MKV, WEBM, VOB, RM, RMVB, M4V, MPG, OG… | 256 GB | none published | Up to 4K. VK recommends 16:9 at 1280x720 or above. | yes | no | no | verified | https://vk.com/@business-kak-rabotat-s-formatami-video-vkontakte | 2026-09-11 |
| VK | A. image attach | JPG, PNG, GIF (per the API). | 50 MB | none published | Sum of height + width <= 14,000 px; each side <= 8,192 px; aspect rat… | yes | unknown | unknown | verified-partial | https://dev.vk.ru/ru/api/upload/photo-in-message | 2026-09-11 |
| Instagram | A. video (Reels) | MOV or MP4 (MPEG-4 Part 14), no edit lists, moov atom at the FRONT of the file.… | 300 MB | 15 min | Max 1920 horizontal pixels. Required aspect ratio between 0.01:1 and … | yes | no | no | verified | https://developers.facebook.com/docs/instagram-platform/instagram-graph-api/reference/ig-user/media/ | 2026-09-11 |
| Weibo | A. video attach | MP4 is the practical/most compatible target. Third-party guides claim MP4/MOV/A… | 15 GB | minimum 3 s (mobile); NO official maximum for any tier | No published pixel limit. Weibo requires submitted videos to be 720p … | yes | no | no | verified | https://kefu.weibo.com/faqdetail?id=21503 | 2026-09-11 |
| Bilibili | A. video attach | Recommended: MP4 and FLV (Bilibili states these are advantaged in transcoding a… | 16 GB hard cap; 64 GB on web for 权益等级 lv3+ (a SEPARATE official Bilibili source gives 4 GB — see note) | 10 h documented for mobile single-P submissions; the current web article states no max | Maximum resolution 4096x4096. Maximum frame rate 120 fps. Peak bitrat… | yes | no | no | verified-conflicting | https://www.bilibili.com/blackboard/help.html#/?qid=3845&pid=312 | 2026-09-11 |
| Facebook Messenger | A. video attach | Not published for the consumer app. The Messenger Platform (Pages/bots) API acc… | 25 MB all asset types / 8 MB image-by-URL (developer API only); consumer app publishes none | none published | Not published. The only duration-like figure in the developer docs is… | yes | unknown | unknown | verified | https://developers.facebook.com/docs/messenger-platform/send-messages/saving-assets/ | 2026-09-11 |
| LINE | A. video attach | Not published by LINE for chat video. Photos in chat/album accept JPEG/PNG/GIF. | 20 MB for ALBUM photos; no published MB cap for a chat video/file | 5 min for ALBUM videos (Premium); no published cap for chat video | ALBUM photos: up to 100 megapixels. Video resolution is not specified… | yes | no | no | verified-partial | https://help.line.me/line/smartphone/sp?contentId=20000248&lang=en | 2026-09-11 |
| LINE | B. sticker | APNG with a .png file extension - explicitly NOT GIF and NOT WebP. Main image i… | 1 MB | 4 s | Maximum 320x270 px, AND either the width or the height must be at lea… | yes | yes | yes | verified | https://creator.line.me/en/guideline/animationsticker/ | 2026-09-11 |
| WeChat | B. sticker | Animated stickers: GIF only. Static: PNG/JPG/GIF. Cover: PNG. Chat panel icon: … | 500 KB | none published | Sticker image exactly 240x240. Cover 240x240. Chat panel icon 50x50. … | yes | yes | yes | verified | https://sticker.weixin.qq.com/cgi-bin/mmemoticon-bin/readtemplate?t=guide%2Findex.html | 2026-09-11 |
| WeChat | A. video attach | MP4 (documented for the WeChat official-account/Official Platform API video pat… | not published for personal chat; Official Platform API: video 10 MB / image 10 MB; chunked video upload up to 2 GB | none published | Not published for personal chat. WeChat Channels (视频号) requires aspec… | yes | no | no | unverified | https://developers.weixin.qq.com/doc/subscription/api/material/permanent/api_addmaterial.html | 2026-09-11 |
| QQ | A. video attach | Official QQ Bot API v2: inline video must be MP4; images accept png/jpg (the me… | Bot API soft 20 MB image / 30 MB video (silently becomes a file), hard 200 MB; consumer chat: not published | none published | Not published. | yes | unknown | no | verified-partial | https://bot.q.qq.com/wiki/develop/api-v2/server-inter/message/rich-media.html | 2026-09-11 |
| QQ | B. sticker | Main sticker: GIF. Thumbnail: PNG. Icons: PNG. | 300 KB | none published | Main sticker EXACTLY 300x300. Thumbnail EXACTLY 300x300. Icons 18x18 … | unknown | yes | yes | verified-partial | https://web.archive.org/web/2023/https://yuanchuang.qq.com/html/design_rule.html | 2026-09-11 |
| KakaoTalk | B. sticker | Animated emoticons must be animated WebP built with Kakao's own WebP Animator t… | 650 KB | no time cap — frame cap instead (<=24 frames big/moving, <=18 mini) | Moving emoticon 360x360; mini 180x180; big 540x540 / 300x540 / 540x30… | yes | yes | yes | verified-partial | https://emoticonstudio.kakao.com/webp-animator | 2026-09-11 |
| KakaoTalk | A. video attach | PC file send accepts images jpg/jpeg/gif/bmp/png/tif/tga/psd and video mp4/m4v/… | 300 MB | none published | Not published. | no-partial | unknown | no | verified-partial | https://cs.kakao.com/helps_html/2128?locale=ja | 2026-09-11 |
| Reddit | A. video attach | MP4 and MOV (stated by Reddit). Reddit also has a native GIF converter that tur… | organic posts: not published. Comments (the only published video spec): under 1 GB, ideally under 300 MB | posts: 15 min per a 2017 announcement only; comments: 2-180 s | posts: not published. Comments: min 50x50 px, min bitrate 1 KB/s. | yes | no | unknown | verified-partial | https://redditinc.com/news/video-is-coming-to-reddit | 2026-09-11 |

## Could not verify (honest gaps — do not fill these with plausible numbers)

1. **WeChat personal-chat video/image limits.** Tencent publishes no readable primary source. Third-party pages attribute "25 MB in-chat video / 5 min / ~200 MB via File" to 腾讯客服, but no Tencent-hosted page stating it could be read.
2. **QQ consumer-chat video/image limits.** Same problem; the official sticker platform page (yuanchuang.qq.com) no longer loads, so only a 2023-or-earlier archived snapshot exists.
3. **LINE chat video size/duration and its codec list.** The "5-minute chat video" and "~1 GB File send" figures appear only in blogs, never on help.line.me. The 5-minute figure that IS official belongs to the ALBUM path.
4. **KakaoTalk mobile limits**, and Kakao emoticon creator-proposal KB caps/cut counts (third-party Korean blogs only).
5. **Facebook Messenger consumer-app limits** — Meta publishes none; only the developer API's 25 MB / 8 MB exist. Messenger also has **no public sticker submission spec at all**.
6. **Reddit native video POST limits** (size, duration, codecs, dimensions). Reddit publishes video specs only for **comments** (<1 GB, 2-180 s). The 2017 post figure (MP4/MOV, 15 min) is nine years old. support.reddithelp.com refused crawling; the comment article was reachable.
7. **Weibo maximum video duration** for any tier, and its container/codec list. Only a MINIMUM (3 s, mobile) is published. The premise that Weibo duration differs by verification level is **unsupported by primary sources**.
8. **Bilibili web-upload max duration**, and whether its sticker file (vs cover) is re-encoded / loops / keeps alpha.
9. **Alpha survival on any server-transcoded video/image path** (WhatsApp, Telegram, Signal, VK, Weibo, Messenger, Discord, Bluesky, Mastodon stills). Only **sticker** pipelines document transparency as required.
10. **Whether Discord accepts GIF stickers** — the API `format_type` enum lists GIF(4), but the help article names only PNG/APNG.
11. **Pixiv ugoira pixel limits and loop wording**; whether normal Pixiv illustration PNG transparency survives display.
12. **X end-user (help.x.com) app-UI limits** — help.x.com is Cloudflare-403 to automated readers. Only the API docs were readable.
13. **TikTok's app-side caps** (~287.6 MB iOS / ~72 MB Android) and the 30/60-minute upload claims — support.tiktok.com is client-rendered and no official page stating them could be read.
14. **Mastodon custom-emoji dimension cap** (none exists in code) and still-image alpha survival.
15. **Instagram feed-VIDEO (non-Reel) size/duration** — Meta publishes Image/Reel/Story specs only.

## Numbers that changed recently (use the new value, not the cached one)

| Fact | Old value | Current value | When |
|---|---|---|---|
| Discord free upload | 10 MB (8 MB before 2023) | **20 MB** — Discord's help article now says 20MB | announced 2026-08-13 |
| Bluesky video | 50 MB / 60 s | **300 MB / 10 min** (100 MB after 2025-03) | Aug 2026 |
| X Post video | "140 s / 512 MB" (still repeated everywhere) | **20 min / 8 GB**; Premium 125 min / 16 GB. 140 s / 512 MB is the **DM** cap | current API docs |
| Bilibili video | 8 GB / 32 GB / 电磁力 tiers (2020-2023 sources) | **16 GB** (64 GB at lv3+ web) per help centre 2026-04-01; its own API doc says 4 GB | 2026-04-01 |
| Pixiv ugoira GIF | 8 MB | **16 MB** (frame count unchanged) | 2022-06-01 |
| Signal attachments | 100 MB (docs silent) | **100 MiB** still the default in source; a 2026 "200 MB" claim is unverified | — |
| Mastodon image matrix (mastodon.social) | upstream default 8.3 MP | **33.2 MP** on mastodon.social — instance drift is real | live 2026-09-11 |
| Instagram Reels | 90 s | 15 min max (API); in-app longer; Meta recommends ≤3 min | — |

## Miscitation traps (each of these is widely repeated and wrong)

- **X**: "140 s / 512 MB" is the DM video cap, not the Post cap.
- **VK**: the circulating "2 GB / 1080p" is VK's **advertising** format spec; organic video is 256 GB / up to 4K. VK's GIF rules are ad-only too.
- **Telegram**: the Bot API's 50 MB (photos 10 MB) is **bot-only**; users get 2 GB (4 GB Premium). 2000 MB applies only to a self-hosted local Bot API server.
- **WhatsApp**: the Cloud API's 16 MB video limit is the **business API**; consumer sends are 100 MB video / 2 GB document. WhatsApp's sticker FAQ documents only the 100 KB **static** rule — the 500 KB animated figure comes from the official sticker-pack repo.
- **Mastodon**: the widely repeated custom-emoji "50 KB" is wrong — current source says **256 KiB**.
- **Mastodon**: no universal limits exist at all; read `GET /api/v2/instance`.
- **QQ**: the soft video limit (30 MB) doesn't error — it silently downgrades the video to a **file attachment**.
- **KakaoTalk**: animated emoticons must be built with Kakao's **own WebP Animator**; own-tool WebP may fail to run.
- **LINE**: LINE's own two documents disagree on animated-sticker size (1 MB Creators Market vs 300 KB Sponsored Stickers guide).
- **Pixiv**: animated GIF and APNG are **rejected** as normal illustrations; they are only valid through the ugoira flow.
- **TikTok**: GIF is not accepted anywhere (video or photo post).

## Export-preset takeaways

- **Alpha is a sticker-only feature.** No platform documents transparency surviving a server-transcoded video or photo. Five sticker pipelines require it: Telegram (WebM/VP9 or TGS), Signal (APNG), LINE (APNG), Discord (APNG/GIF), WhatsApp (animated WebP), plus Discord/Telegram/Mastodon/WeChat emoji.
- **The four hard, machine-checkable sticker specs worth encoding:** Telegram video sticker (512 exact, ≤256 KB, ≤3 s, ≤30 fps, no audio, VP9/WebM); LINE animated (≤320x270 with one side ≥270, 5-20 frames, ≤4 s, 1-4 loops, ≤1 MB, APNG); WhatsApp animated (512x512 exact, ≤500 KB, ≤10 s, ≥8 ms/frame, animated WebP only); Signal (512x512, ≤300 KB, ≤3 s, APNG only).
- **Looping:** stickers loop almost everywhere; WeChat requires it; Telegram "all animations must be looped". Exceptions: KakaoTalk stops on the last frame in chat by default, and LINE caps at 1-4 loops rather than infinite.
- **Where re-encoding is documented and therefore quality-target-setting matters:** YouTube, X, TikTok, Instagram, Discord, Bluesky, Mastodon, Pixiv, Weibo, Bilibili, Signal, Telegram (media path).
- **Where it is NOT documented and we should assume worst case:** VK, QQ, WeChat, Messenger, LINE chat video, Reddit.
