Check with any coders before redoing the installation to prevent them from losing any work.  They should move their code to prod or back it up.

## Backup Boa DB

From Boa:

@dump-d
cp sindome.db.new boa-MM-DD-YY.sindome.db.new

## Backup Prod to Boa
There is a readme on Prod MOO server with the scp for copying the prod db to boa, it has a key to access boa already

## [OLD]  Download Prod DB
Download the DB from FTP or via SCP

## [OLD] Upload the Prod  DB using SCP

change the file name!

scp -i slither.pem dbs/10-22-16-sindome.db.new ubuntu@ec2-54-173-190-176.compute-1.amazonaws.com:/home/ubuntu

## GZIP the files on prod

tar --exclude='./files/eventlog-04-01-17.log' -zcvf ~/files_MM_DD_YY.tar.gz files
scp -i slither.pem files_MM_DD_YY.tar.gz ubuntu@ec2-54-173-190-176.compute-1.amazonaws.com:/home/ubuntu

Unzip:  tar xvzf file.tar.gz

## Rsync the files from prod to boa (UNTESTED)
The boa .pem file should be present as slither.pem on prod, but if not:

scp slither.pem slither@sindome.org:/users/slither

To rsync the files (UNTESTED!!):

rsync -ravz -e "ssh -i slither.pem" /Users/sindome/Servers/sindome-moo/files ubuntu@ec2-54-173-190-176.compute-1.amazonaws.com:/home/ubuntu

## Restart MOO
1. cd /sindome-moo/bin
2. mv /sindome-moo/db/NEWDB.db sindome.db
3. shutdown boa ;shutdown()

Bring the MOO up (BEST SOLUTION)
1. cd /themoo/sindome-moo/bin
2. sudo ./restart.sh sindome 5555
This will bring it up with sindome.log intact

Bring the MOO up (less good)
// must use sudo if we want things to work right since we are mv'ing files with root. would not do this in prod
1. cd /themoo/sindome-moo
2. sudo nohup ./moo db/sindome.db db/sindome.db.new &
3. control + z (move process to background or it will die with shell)

OR (not as good since it runs in foreground or something
1. cd /themoo/sindome-moo/bin
2. ./restart.sh sindome 5555

## Set Welcome Message

;#10.welcome_message = #10.welcome_message_boa

## Promote Coders to $agent

// meph
@chparent #40142 to $agent

## Promote Dev Coders to $creator
@programmer Mench
@chparent #55042 to $creator

## Reset Player Passwords
;for p in ($ou:descendents(#131)) if (!$wiz_utils:is_admin(p)) clear_property(p, "password"); endif endfor

## stop slack trying to send
@program $browser:_post and make it return at top

## stop groups without the memento files on disk from spawning
@set #54569.auto_spawn to 0


## stop reading from files that don't exist
;$scheduler:remove_scheduled(#18056, "pull_unread_gridmail")
;$scheduler:remove_scheduled(#24, "get_blocked_proxy_ips")
## Unlisten 80
per johnny we should close this due to security issues
;unlisten(80)

# Updating Webclient
cp ~/favicon-boa.ico /dome-client.js/public
