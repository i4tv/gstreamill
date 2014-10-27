
gstreamillconfdir = /usr/local/share/gstreamill/conf
gstreamillconf_DATA = \
        local/share/gstreamill/conf/gstreamill.conf

admindir = $(datadir)/gstreamill/admin
admin_DATA = \
        local/share/gstreamill/admin/index.html \
        local/share/gstreamill/admin/header.html \
        local/share/gstreamill/admin/footer.html \
        local/share/gstreamill/admin/newlivejob.html \
        local/share/gstreamill/admin/joblist.html \
        local/share/gstreamill/admin/newtranscodejob.html \
        local/share/gstreamill/admin/jobmanage.html \
        local/share/gstreamill/admin/jobstatus.html \
        local/share/gstreamill/admin/system.html \
        local/share/gstreamill/admin/gstreamill.html \
        local/share/gstreamill/admin/setnetwork.html \
        local/share/gstreamill/admin/mediaman.html

admincssdir = $(datadir)/gstreamill/admin/css
admincss_DATA = \
        local/share/gstreamill/admin/css/bootstrap.css \
        local/share/gstreamill/admin/css/bootstrap-theme.css \
        local/share/gstreamill/admin/css/gstreamill.css

adminjsdir = $(datadir)/gstreamill/admin/js
adminjs_DATA = \
        local/share/gstreamill/admin/js/bootstrap.js \
        local/share/gstreamill/admin/js/bootbox.js \
        local/share/gstreamill/admin/js/jquery.js \
        local/share/gstreamill/admin/js/i18next.js \
        local/share/gstreamill/admin/js/gstreamill.js \
        local/share/gstreamill/admin/js/resumable.js 

adminfontsdir = $(datadir)/gstreamill/admin/fonts
adminfonts_DATA = \
        local/share/gstreamill/admin/fonts/glyphicons-halflings-regular.eot \
        local/share/gstreamill/admin/fonts/glyphicons-halflings-regular.svg \
        local/share/gstreamill/admin/fonts/glyphicons-halflings-regular.ttf \
        local/share/gstreamill/admin/fonts/glyphicons-halflings-regular.woff

adminimgdir = $(datadir)/gstreamill/admin/img
adminimg_DATA = \
        local/share/gstreamill/admin/img/logo.png \
        local/share/gstreamill/admin/img/waiting.gif

adminschemasdir = $(datadir)/gstreamill/admin/schemas
adminschemas_DATA = \
        local/share/gstreamill/admin/schemas/live_source_test.json \
        local/share/gstreamill/admin/schemas/live_source_v4l2.json \
        local/share/gstreamill/admin/schemas/live_source_network.json \
        local/share/gstreamill/admin/schemas/live_encoder_phone.json \
        local/share/gstreamill/admin/schemas/live_encoder_sdtv.json \
        local/share/gstreamill/admin/schemas/live_encoder_hdtv.json \
        local/share/gstreamill/admin/schemas/transcode_source_decodebin.json \
        local/share/gstreamill/admin/schemas/transcode_encoder_phone_mp4.json \
        local/share/gstreamill/admin/schemas/transcode_encoder_pad_mp4.json \
        local/share/gstreamill/admin/schemas/transcode_encoder_tv_mp4.json \
        local/share/gstreamill/admin/schemas/transcode_encoder_hdtv_mp4.json

localesdir = $(datadir)/gstreamill/admin/locales/dev
locales_DATA = \
        local/share/gstreamill/admin/locales/dev/translation.json

localesendir = $(datadir)/gstreamill/admin/locales/en
localesen_DATA = \
        local/share/gstreamill/admin/locales/en/translation.json

localeszh_CNdir = $(datadir)/gstreamill/admin/locales/zh_CN
localeszh_CN_DATA = \
        local/share/gstreamill/admin/locales/zh_CN/translation.json

EXTRA_DIST = lib local

install-exec-hook:
	test -s /usr/lib/systemd/system && { \
	    install -c -m 644 lib/systemd/system/gstreamill.service '/usr/lib/systemd/system'; \
	} || true
