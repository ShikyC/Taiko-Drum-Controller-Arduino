#Sanro - Arduino
太鼓三郎（仮）用のハードウェア（タタコン）

##プログラムについて

東アジアの音ゲーマーならほどんどみんなが「太鼓の達人」という[バンダイナムコエンターテインメント](http://bandainamcoent.co.jp/)が開発した有名なゲームを知っていると思います。

このオープンソースプログラムの目的は家でアーケード感覚で使える「太鼓の達人」のハードウェア（つまりタタコン、以下タタコンと呼ぶ）を作ることです。

**＊＊＊＊＊＊＊このプログラムは私用なげなためのものです、商業/営業用などの使用は厳禁＊＊＊＊＊＊＊＊**
**＊＊＊＊＊＊＊自分で設計し作ったタタコンを市場に流通させないでください＊＊＊＊＊＊＊＊＊**

##特徴/出来ること

* **PCゲーム「太鼓さん次郎」に完全対応**正確に、キーボードさえ使っていればどんなアプリケーション、ゲーム、エミュレーターでも使用可能。
* **圧力感知式** 今は「太鼓三郎」というオープンソースのゲームを開発中、これに対応しています。
* **高密度の入力に対応、ローリングなど（ダブルストロークとも）**

*　一言でいうと、完全アーケード感覚で太鼓を叩ける、調整さえよく出来れば! :D*

##予備知識

DIYプロジェクトなため、以下の知識が必要
* 電子に関する基本な知識、
* ジャンパー線とブレッドボードとマイクロプロセッサの繋ぎ方。
* **はんだ付けに関する知識、必須ではないが**

##作業開始

タタコンの組み立てと調整は数日かかるかもしれない。
マイクロプロセッサチップは高くないので、好きな所で購入してください。

###準備する物

ほどんどは代替出来ます、ここで私が使った物をリスト

* [Arduino Micro](http://i.imgur.com/lXqnpJ9.jpg) モジュール/基盤 x 1
* [KEYES K-036](http://i.imgur.com/gUWnUCc.png) マイクロフォンモモジュール/基盤 x 4　（以下、フォンモジュール）
* ブレードボード x 1
* ジャンパー線　多数
* Micro USB ケーブル x 1
* [Arduino IDE](https://www.arduino.cc/en/Main/Software)

その他諸々、たとえば

* 木版、[出来れば形はこう](http://i.imgur.com/va20eVn.jpg)
* 木版の下敷きになるスポンジ　（元文：Foamed plastics）
* 接着剤
* スクリュードライバーやマルチメーターなどの電子用具

注意すべきこと:

1.ATmega32u4を使用したarduino基盤もしくはarduino　Dueとarduino　Zero基盤には対応しています、Arduino Microは一番安いですが.  
2.ブレードボードは安いですが、一番いい/安定している選択ではない、はんだ付け用のPCBの図面は提供してあります、詳細は下の「ＰＣＢ製作」をご覧ください.   
3.マイクロフォンモモジュールは自作でもOK、arduino基盤との繋ぎ方が分かれば.  
4.厚い/無垢板/密度高い/重い木版が一番、これと逆に、合板/プラ版/中密度繊維板（MDF）は外縁部分が脆くて壊れやすい。いい感覚を得るため、[木版の形はこうであるべき](http://i.imgur.com/va20eVn.jpg)、切断工具がないなら、普通の形の木版も問題ない.  

パーツの繋ぎ方

回路はすごくシンプルです、抵抗もキャパシタも要らない、**要るのはジャンパー線だけ**  

ひとつのフォンモジュールには四つのピンがあり、使用するのはその中ほ三つだけ（`A0`, `+`, と `G`）、それぞれ（四つ）の`A0`のoutput端子をArduino Microの`A0`から`A3`input端子に接続。あとはそれぞれの`+`ピンを全部Arduino Microの`5V`ピンに、それぞれ`G`ピンはArduino Microの`GND`ピンに。下の写真を見て、間違っていないかを確認しましょう。
（写真は後でアップロードるす）  

###（非必須）ＰＣＢ製作

ＰＣＢは自作もしくはPCB製作業者に任せることが出来ます。  自作なら、以下の物が必要  （訳者：確信ないので、英語は保留している）
* 2.2 x 1.6 inches Empty PCB x 1　空のPCB板
* Thermal transfer paper x 1　熱転写紙
* Laser printer x 1　レーザープリンター
* Standard 4-pin header x 4　スタンダード4ピン端子
* Thermal transfer machine (or clothes iron)　熱転写機（アイロンもオッケー）
* Etchant　エッチング剤
* Soldering tools　はんだ付け用の工具

ＰＣＢ自作の工程は完全別の話になりますので、[この動画](https://www.youtube.com/watch?v=mv7Y0A9YeUc)を見て参考になってください。
製作に使うファイルは`Eagle/sanro-arduino`フォルダーの`sanro.eps` にまとめています。
図面をダウンロードして編集もできます、その時は[Eagle Software](http://www.cadsoftusa.com/download-eagle/)を使ってください。

###プログラムをarduino基盤にアップロード

1. [Arduino IDE](https://www.arduino.cc/en/Main/Software)をダウンロードし、インストール。  
2. 新規フォルダーを作って、ソースファイル(`sanro.ino` and `cache.h`)をそこに入れます  
3. usbケーブルを使って、Arduino Microをパソコンに接続、デバイスドライバは自動的にインストールされているはず（Arduino IDEインストールした時）、なにか問題があった場合、この[オフィシャルガイド](https://www.arduino.cc/en/Guide/ArduinoLeonardoMicro#toc8)を見てください  
4. Arduino IDEで`sanro` projectを開く.  
5. メニューで"Board" - "Arduino/Genuino Micro" を選択.  
6. コンパイルしてアップロード.  

##コンフィギュレーション

** *警告：フォンモジュールと木板の状況に個人差は激しいので、ハードウェアとプログラムのパラメーターの調整には大量な時間がかかります、どうか耐えてください、この先には多くの試行錯誤が待っています。***

###ハードウェア  
やることは二つだけ  　　
1. フォンモジュールを木板に接着する（訳者：ねじを使う方がいい気がする）  　　
2. 木版をスポンジに固定  　　

でもどうやるかは問題です、基本的に以下コツがあります    
* フォンモジュールは出来るだけ木版に近い、かつ固く固定すること    
* マイクロフォンを外部の音から遮断すること    
* 木版をお互い接触させないこと    

これらを実現するにひとつの方法は、フォンモジュールの受信機(元文：receiver)（[わたしが使ったの](http://i.imgur.com/gUWnUCc.png)を含め、受信機は大体円柱状です）のフルター（円柱の上にある黒い布）を撤去、この面を木板にくっつく、接着(元文：seal)。こうやって木板から発する音は直接マイクロフォンに伝わる、ラウド・アンド・クリア。同時に隣接の木板の音を最小限に減らせるができます。  

**(�ここからの部分は専門用語多いので、間違うと訳出来ないところが多い、分かる方々ご協力お願いします)**  

あと注意すべきこと、このKEYESモジュールにはポテンショメータがひとつあります、このポテンショメータはマイクロフォンのquiescent operating point (Q-point)を調整用に使うものですが、私はアルゴリズムを使って、各マイクロフォンのQ-pointの違いによって出来たバイアスを消去することができた。**手動でこのポテンショメータを調整、Q-pointを同じぐらいにすののがいい**どうやるかは提供会社に尋ねてください。

###プログラム中のパラメーター
フォンの状況によって`LIGHT_THRES` と`HEAVY_THRES`の数値をかえてください。
コードは短い、説明はいらない、理解したい方は「アルゴリズムについて」の部分を見てください。

##アルゴリズムについて
このプラグラムのアルゴリズムはシンプル、でもまだ改善する余地はあります、pull requestを歓迎しています。

簡単に言うと、アナログ信号を受けた後は以下四つの演算をする
**（専門用語わからないので日本語訳はこれでおわり、あとは分かる方々お願いします）**
1. Calculate the derivative
2. Calculate the power of the waveform
3. Calculate the convolution of the power
4. Find the peak of the power convolution and compare it with the thresholds to see if there is a light or heavy hit

This picture shows the algorithm in a clearer way:

(Picture to be uploaded)

Step 1 is to elinimate the difference of Q-point, which makes it easier and more accurate to calculate the power of the waveform.

Step 2, calculating the power of the waveform, can also enhance the signal to noise ratio (SNR), which can further eliminate the noises. `LIGHT_THRES` is also used here to cut the low-power noises out.

Step 3 is to "polish" the power curve to make it more like a sequence of hit pulses, which makes it easier to find the power peak.

For Arduino microprocessors, the executing time for each loop is not stable - the processor always executes the instructions as fast as it can. Onece the loop ends, it immediately starts the next loop. This is extremely bad for sampling sounds. Therefore, the program implements a simple sampling frequency control mechanism to restrict the sampling frequency to no more than 1,000Hz.





















