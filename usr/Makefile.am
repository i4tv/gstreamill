
admindir = /usr/share/gstreamill/admin
admin_DATA = \
        share/gstreamill/admin/index.html \
        share/gstreamill/admin/header.html \
        share/gstreamill/admin/footer.html \
        share/gstreamill/admin/newlivejob.html \
        share/gstreamill/admin/joblist.html \
        share/gstreamill/admin/newtranscodejob.html \
        share/gstreamill/admin/jobmanage.html \
        share/gstreamill/admin/jobstatus.html \
        share/gstreamill/admin/system.html \
        share/gstreamill/admin/gstreamill.html \
        share/gstreamill/admin/setnetwork.html \
        share/gstreamill/admin/mediaman.html

admincssdir = /usr/share/gstreamill/admin/css
admincss_DATA = \
        share/gstreamill/admin/css/bootstrap.css \
        share/gstreamill/admin/css/bootstrap-theme.css \
        share/gstreamill/admin/css/gstreamill.css

adminjsdir = /usr/share/gstreamill/admin/js
adminjs_DATA = \
        share/gstreamill/admin/js/bootstrap.js \
        share/gstreamill/admin/js/bootbox.js \
        share/gstreamill/admin/js/jquery.js \
        share/gstreamill/admin/js/i18next.js \
        share/gstreamill/admin/js/gstreamill.js \
        share/gstreamill/admin/js/resumable.js 

adminfontsdir = /usr/share/gstreamill/admin/fonts
adminfonts_DATA = \
        share/gstreamill/admin/fonts/glyphicons-halflings-regular.eot \
        share/gstreamill/admin/fonts/glyphicons-halflings-regular.svg \
        share/gstreamill/admin/fonts/glyphicons-halflings-regular.ttf \
        share/gstreamill/admin/fonts/glyphicons-halflings-regular.woff

adminimgdir = /usr/share/gstreamill/admin/img
adminimg_DATA = \
        share/gstreamill/admin/img/logo.png \
        share/gstreamill/admin/img/waiting.gif

adminschemasdir = /usr/share/gstreamill/admin/schemas
adminschemas_DATA = \
        share/gstreamill/admin/schemas/live_source_test.json \
        share/gstreamill/admin/schemas/live_source_v4l2.json \
        share/gstreamill/admin/schemas/live_source_network.json \
        share/gstreamill/admin/schemas/live_encoder_phone.json \
        share/gstreamill/admin/schemas/live_encoder_sdtv.json \
        share/gstreamill/admin/schemas/live_encoder_hdtv.json \
        share/gstreamill/admin/schemas/transcode_source_decodebin.json \
        share/gstreamill/admin/schemas/transcode_encoder_phone_mp4.json \
        share/gstreamill/admin/schemas/transcode_encoder_pad_mp4.json \
        share/gstreamill/admin/schemas/transcode_encoder_tv_mp4.json \
        share/gstreamill/admin/schemas/transcode_encoder_hdtv_mp4.json \
        share/gstreamill/admin/schemas/transcode_encoder_phone_m3u8.json \
        share/gstreamill/admin/schemas/transcode_encoder_pad_m3u8.json \
        share/gstreamill/admin/schemas/transcode_encoder_tv_m3u8.json \
        share/gstreamill/admin/schemas/transcode_encoder_hdtv_m3u8.json

localesdir = /usr/share/gstreamill/admin/locales/dev
locales_DATA = \
        share/gstreamill/admin/locales/dev/translation.json

localesendir = /usr/share/gstreamill/admin/locales/en
localesen_DATA = \
        share/gstreamill/admin/locales/en/translation.json

localeszh_CNdir = /usr/share/gstreamill/admin/locales/zh_CN
localeszh_CN_DATA = \
        share/gstreamill/admin/locales/zh_CN/translation.json

EXTRA_DIST = lib local

install-exec-hook:
	ps -p1 | grep systemd && { \
	    mkdir -p /usr/lib/systemd/system; \
	    sed -e 's:$$(bindir):$(bindir):' lib/systemd/system/gstreamill.service.in > lib/systemd/system/gstreamill.service; \
	    install -c -m 644 lib/systemd/system/gstreamill.service '/usr/lib/systemd/system'; \
	} || true
