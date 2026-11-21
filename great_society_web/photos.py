import requests,json,re,time
from pathlib import Path
root=Path(__file__).parent/'dist'; (root/'photos').mkdir(exist_ok=True)
s=requests.Session();s.headers['User-Agent']='HistoryLearningReport/1.0 (educational local report)'
queries=[('us','Lyndon Johnson signing Medicare 1965'),('policy','Lyndon Johnson elementary school 1965'),('results','March Washington 1963 crowd'),('cn1','Beijing 1973 street filetype:bitmap'),('cn2','China 1980 bicycle'),('cn2','Shanghai 1991 street filetype:bitmap'),('cn3','Beijing elderly park'),('pressure','China rural school classroom')]
rows=[]
for i,(section,q) in enumerate(queries):
 try:
  r=s.get('https://commons.wikimedia.org/w/api.php',params=dict(action='query',format='json',generator='search',gsrsearch=q,gsrnamespace=6,gsrlimit=8,prop='imageinfo',iiprop='url|extmetadata',iiurlwidth=1200),timeout=45);r.raise_for_status()
  pages=sorted(r.json().get('query',{}).get('pages',{}).values(),key=lambda x:x.get('index',100))
  for page in pages:
   if not re.search(r'\.(jpg|jpeg|png)$',page['title'],re.I):continue
   info=page.get('imageinfo',[{}])[0];meta=info.get('extmetadata',{});license=meta.get('LicenseShortName',{}).get('value','')
   url=info.get('thumburl',info.get('url',''))
   if not any(x in license.lower() for x in ('cc','public domain')) or not re.search(r'\.(jpg|jpeg|png)(?:\?|$)',url,re.I):continue
   if any(row['page']==info['descriptionurl'] for row in rows):continue
   response=s.get(url,timeout=60)
   if response.status_code!=200:continue
   file=f'photos/{i}.jpg';(root/file).write_bytes(response.content)
   clean=lambda field:re.sub('<[^>]+>','',meta.get(field,{}).get('value','')).strip()
   rows.append(dict(section=section,file=file,title=page['title'].removeprefix('File:'),description=clean('ImageDescription')[:450],date=clean('DateTimeOriginal'),artist=clean('Artist'),license=license,license_url=clean('LicenseUrl'),page=info['descriptionurl']))
   print(i,page['title'],license,flush=True);break
 except Exception as e:print('failed',q,str(e),flush=True)
 time.sleep(.5)
(root/'photos.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('photos',len(rows))
