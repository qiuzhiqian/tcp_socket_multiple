##tcp服务器

这是一个多线程的tcp服务器，同时支持数据的发送和接受
支持的传输类型有两种:
  - 透明传输
  - 协议传输

数据导出有两种方式:
  - 直接导出到控制台
  - 导出到文件中

服务器端支持的命令:
  - list 列出当前在线的客户端列表信息
  - kill 结束自定客户端连接线程
  - exit 服务器退出
  - sendx 向指定的服务器发送数据,其中x为客户端的索引号
  - modex 切换传输模式,x为模式代号,=0表示为透明传输,=1表示为协议传输
