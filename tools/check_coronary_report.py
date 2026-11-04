"""Check the generated report using an already-running local headless Edge CDP session."""
import base64
import json
from pathlib import Path
import time
from urllib.request import urlopen

import websocket

ROOT=Path(__file__).resolve().parents[1]


def main():
    targets=json.load(urlopen('http://localhost:9327/json'))
    target=next(t for t in targets if t['type']=='page')
    connection=websocket.create_connection(target['webSocketDebuggerUrl'],origin='http://localhost:9327',timeout=15)
    serial=0
    exceptions=[]

    def call(method,params=None):
        nonlocal serial
        serial+=1
        connection.send(json.dumps({'id':serial,'method':method,'params':params or {}}))
        while True:
            response=json.loads(connection.recv())
            if response.get('method')=='Runtime.exceptionThrown':
                exceptions.append(response['params'])
            if response.get('id')==serial:
                if 'error' in response:
                    raise RuntimeError(response['error'])
                return response.get('result',{})

    def evaluate(expression):
        result=call('Runtime.evaluate',{'expression':expression,'returnByValue':True,'awaitPromise':True})
        if 'exceptionDetails' in result:
            raise RuntimeError(result['exceptionDetails'])
        return result.get('result',{}).get('value')

    def wait_render():
        evaluate('new Promise(resolve=>setTimeout(resolve,250))')

    def screenshot(name):
        data=call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
        destination=ROOT/'build'/name
        destination.write_bytes(base64.b64decode(data['data']))

    call('Page.enable');call('Runtime.enable');call('Network.enable')
    call('Network.setBlockedURLs',{'urls':['http://*','https://*']})
    call('Emulation.setDeviceMetricsOverride',{'width':1440,'height':1100,'deviceScaleFactor':1,'mobile':False})
    call('Page.navigate',{'url':(ROOT/'docs/traditional_coronary_report.html').as_uri()})
    for _ in range(50):
        if evaluate('document.readyState')=='complete' and evaluate('Array.from(document.images).every(i=>i.complete&&i.naturalWidth>0)'):
            break
        time.sleep(.1)
    else:
        raise RuntimeError('Report did not finish loading')
    assert evaluate('document.querySelectorAll(".method").length')==5
    assert evaluate('document.querySelector("#case-select").options.length')==11
    assert evaluate('document.querySelector("#case-stats").textContent.includes("0.2191")')
    for kind,count in [('cta',2),('component',2),('xca',1),('all',5)]:
        evaluate(f'document.querySelector("[data-filter={kind}]").click()')
        assert evaluate('document.querySelectorAll(".method:not([hidden])").length')==count
    before=evaluate('document.querySelector("#case-image").src')
    evaluate('document.querySelector("#case-select").value="1";document.querySelector("#case-select").dispatchEvent(new Event("change"))')
    assert evaluate('document.querySelector("#case-stats").textContent.includes("0.8162")')
    assert evaluate('document.querySelector("#case-image").src')!=before
    evaluate('document.querySelector("#case-select").value="111";document.querySelector("#case-select").dispatchEvent(new Event("change"))')
    wait_render();screenshot('coronary-report-desktop.png')
    assert evaluate('document.documentElement.scrollWidth<=innerWidth')
    evaluate('document.querySelector("#diagnosis").scrollIntoView({behavior:"instant"})')
    wait_render();screenshot('coronary-report-diagnosis.png')
    call('Emulation.setDeviceMetricsOverride',{'width':390,'height':844,'deviceScaleFactor':1,'mobile':True})
    evaluate('window.scrollTo({top:0,behavior:"instant"})');wait_render()
    assert evaluate('document.documentElement.scrollWidth<=innerWidth'), 'Mobile page overflows viewport'
    screenshot('coronary-report-mobile.png')
    evaluate('document.querySelector("#methods").scrollIntoView({behavior:"instant"})');wait_render()
    screenshot('coronary-report-mobile-methods.png')
    broken=evaluate("Array.from(document.querySelectorAll('a')).filter(a=>a.getAttribute('href').startsWith('#')&&!document.getElementById(a.hash.slice(1))).map(a=>a.hash)")
    assert not broken,broken
    assert not exceptions,exceptions
    for report in ['segmentation_optimization', 'evaluation_and_industry', 'nnunet_hybrid']:
        call('Emulation.setDeviceMetricsOverride',{'width':1440,'height':1100,'deviceScaleFactor':1,'mobile':False})
        call('Page.navigate', {'url': (ROOT / f'docs/{report}.html').as_uri()})
        for _ in range(100):
            if evaluate('document.readyState') == 'complete' and evaluate('Array.from(document.images).every(i=>i.complete&&i.naturalWidth>0)'):
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Page failed to load: ' + report)
        assert evaluate('document.documentElement.scrollWidth<=innerWidth'), report
        screenshot(report + '-desktop.png')
        if report == 'segmentation_optimization':
            assert evaluate('document.querySelector("#case").options.length') == 11
            before = evaluate('document.querySelector("#comparison").src')
            evaluate('document.querySelector("#case").value="1";document.querySelector("#case").dispatchEvent(new Event("change"))')
            assert evaluate('document.querySelector("#comparison").src') != before
        elif report == 'evaluation_and_industry':
            assert evaluate('document.querySelectorAll("section:last-of-type .scroll tr").length') == 23
            assert evaluate('document.body.textContent.includes("ASSD")')
        else:
            assert evaluate('document.querySelector(".flow").textContent.includes("Frangi")')
            assert evaluate('document.body.textContent.includes("checkpoint_count")')
            assert evaluate('document.querySelectorAll(".table-scroll table").length') >= 2
        call('Emulation.setDeviceMetricsOverride',{'width':390,'height':844,'deviceScaleFactor':1,'mobile':True})
        wait_render()
        assert evaluate('document.documentElement.scrollWidth<=innerWidth'), 'Mobile overflow: ' + report
        screenshot(report + '-mobile.png')
    assert not exceptions, exceptions
    connection.close()
    print('PASS: offline loading, 5 method cards, filters (2/2/1), 11 case options, image switching, anchors, no JS errors, desktop/mobile layout.')


if __name__=='__main__':
    main()
